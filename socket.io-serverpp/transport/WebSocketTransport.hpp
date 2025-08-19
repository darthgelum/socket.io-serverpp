#ifndef SOCKETIO_SERVERPP_WEBSOCKET_TRANSPORT_HPP
#define SOCKETIO_SERVERPP_WEBSOCKET_TRANSPORT_HPP

#include "Transport.hpp"
#include "../config.hpp"
#include "../Logger.hpp"
#include "../uuid.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace transport {

namespace beast = boost::beast;             // from <boost/beast.hpp>
namespace http = beast::http;               // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;     // from <boost/beast/websocket.hpp>
using tcp = boost::asio::ip::tcp;

/**
 * @brief WebSocket transport implementation using Boost.Beast
 */
class WebSocketTransport : public Transport {
public:
    explicit WebSocketTransport(asio::io_service& io_service)
        : m_io_service(io_service),
          m_acceptor(io_service),
          m_socket(io_service) {}

    ~WebSocketTransport() override {
        stop();
    }

    void set_event_handler(std::shared_ptr<TransportEventHandler> handler) override {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        m_event_handler = handler;
    }

    void listen(const std::string& address, int port) override {
        try {
            tcp::endpoint endpoint{boost::asio::ip::make_address(address), static_cast<unsigned short>(port)};
            m_acceptor.open(endpoint.protocol());
            m_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
            m_acceptor.bind(endpoint);
            m_acceptor.listen(boost::asio::socket_base::max_listen_connections);
            LOG_INFO("WebSocket(Beast) listening on ", address, ":", port);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start Beast WebSocket transport: ", e.what());
            throw SocketIOException("Failed to start WebSocket transport", SocketIOErrorCode::CONNECTION_FAILED);
        }
    }

    void start_accept() override {
        do_accept();
        LOG_DEBUG("WebSocket(Beast) started accepting connections");
    }

    bool send_message(const ConnectionHandle& connection,
                      const std::string& message,
                      bool is_binary = false) override {
        std::shared_ptr<class Session> session;
        {
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            auto it = m_sessions.find(connection.id);
            if (it == m_sessions.end()) {
                LOG_WARN("Invalid connection handle for send");
                return false;
            }
            session = it->second;
        }
        if (!session) return false;
        session->send(message, is_binary);
        return true;
    }

    void close_connection(const ConnectionHandle& connection,
                          int code = 1000,
                          const std::string& reason = "Normal closure") override {
        std::shared_ptr<class Session> session;
        {
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            auto it = m_sessions.find(connection.id);
            if (it == m_sessions.end()) {
                LOG_WARN("Invalid connection handle for close");
                return;
            }
            session = it->second;
        }
        if (!session) return;
        session->close(code, reason);
    }

    void stop() override {
        try {
            // Stop accepting new connections
            beast::error_code ec;
            m_acceptor.close(ec);
            if (ec) {
                LOG_WARN("Error closing acceptor: ", ec.message());
            }

            // Close all active sessions
            std::unordered_map<ConnectionId, std::shared_ptr<class Session>> copy;
            {
                std::lock_guard<std::mutex> lock(m_connections_mutex);
                copy = m_sessions;
            }
            for (auto& kv : copy) {
                if (kv.second) kv.second->close(static_cast<int>(websocket::close_code::going_away), "Server shutdown");
            }

            // Clear mappings
            std::lock_guard<std::mutex> lock2(m_connections_mutex);
            m_connections.clear();
            m_sessions.clear();
            LOG_INFO("WebSocket(Beast) transport stopped");
        } catch (const std::exception& e) {
            LOG_WARN("Error stopping WebSocket transport: ", e.what());
        }
    }

    std::string get_name() const override { return "websocket"; }
    bool supports_binary() const override { return true; }

    std::shared_ptr<ConnectionInfo> get_connection_info(const ConnectionHandle& connection) const override {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        auto it = m_connections.find(connection.id);
        if (it != m_connections.end()) return it->second;
        return nullptr;
    }

private:
    // Forward-declare session for use in maps
    class Session : public std::enable_shared_from_this<Session> {
    public:
        struct OutMsg { std::string data; bool binary; };

                Session(WebSocketTransport& parent, tcp::socket socket)
                        : m_parent(parent),
                            m_ws(std::move(socket)) {}

        void run() {
            // Read HTTP request first to inspect headers/target
            auto self = shared_from_this();
            http::async_read(m_ws.next_layer(), m_buffer, m_req,
                boost::asio::bind_executor(self->m_ws.get_executor(),
                    [self](beast::error_code ec, std::size_t) {
                        if (ec) {
                            self->fail("http_read", ec);
                            return;
                        }
                        self->on_http_request();
                    }));
        }

        void send(const std::string& data, bool binary) {
            auto self = shared_from_this();
            boost::asio::post(self->m_ws.get_executor(), [self, data, binary] {
                bool writing = !self->m_out_queue.empty();
                self->m_out_queue.push_back(OutMsg{data, binary});
                if (!writing) self->do_write();
            });
        }

        void close(int code, const std::string& reason) {
            websocket::close_reason cr;
            cr.code = static_cast<websocket::close_code>(code);
            cr.reason = reason;
            auto self = shared_from_this();
            boost::asio::post(self->m_ws.get_executor(), [self, cr] {
                self->m_ws.async_close(cr, [self](beast::error_code ec2) {
                    if (ec2) self->fail("close", ec2);
                });
            });
        }

        const ConnectionId& id() const { return m_id; }

    private:
        void on_http_request() {
            // Parse target and headers, compute or adopt connection id
            std::string target = std::string(m_req.target());
            std::map<std::string, std::string> query_params;
            parse_query_parameters(target, query_params);

            auto sid_it = query_params.find("sid");
            if (sid_it != query_params.end() && !sid_it->second.empty()) {
                m_id = sid_it->second;
                m_is_upgrade = true; // Treat as upgrade of existing session
            } else {
                m_id = m_parent.generate_connection_id();
                m_is_upgrade = false;
            }

            // Prepare connection info
            auto info = std::make_shared<ConnectionInfo>(m_id);
            try {
                auto ep = m_ws.next_layer().remote_endpoint();
                info->remote_address = ep.address().to_string() + ":" + std::to_string(ep.port());
            } catch (...) {
                info->remote_address = "";
            }
            auto ua_it = m_req.find(http::field::user_agent);
            if (ua_it != m_req.end()) info->user_agent = std::string(ua_it->value());
            info->query_params = query_params;
            for (auto const& h : m_req) {
                info->headers[std::string(h.name_string())] = std::string(h.value());
            }

            // Register session and info with the parent
            m_parent.register_session(shared_from_this(), info);

            // Accept the websocket upgrade
            m_ws.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& res) {
                    res.set(http::field::server, std::string("socket.io-serverpp/beast"));
                }));

            auto self = shared_from_this();
            m_ws.async_accept(m_req, boost::asio::bind_executor(self->m_ws.get_executor(), [self](beast::error_code ec) {
                if (ec) {
                    self->fail("accept", ec);
                    self->on_closed_internal(1002, "Handshake failed");
                    return;
                }
                self->on_accepted();
            }));
        }

        void on_accepted() {
            // Notify only for new connections (not upgrades)
            if (!m_is_upgrade) {
                if (auto handler = m_parent.get_event_handler()) {
                    auto info = m_parent.get_connection_info(ConnectionHandle(m_id));
                    if (info) handler->on_connection_open(*info);
                }
            }
            do_read();
        }

        void do_read() {
            auto self = shared_from_this();
            m_ws.async_read(m_read_buffer, boost::asio::bind_executor(self->m_ws.get_executor(),
                [self](beast::error_code ec, std::size_t bytes_transferred) {
                    boost::ignore_unused(bytes_transferred);
                    if (ec) {
                        if (ec == websocket::error::closed) {
                            self->on_closed_internal(1000, "WebSocket closed");
                            return;
                        }
                        self->fail("read", ec);
                        self->on_closed_internal(1006, ec.message());
                        return;
                    }
                    bool is_binary = self->m_ws.got_binary();
                    std::string payload{beast::buffers_to_string(self->m_read_buffer.data())};
                    self->m_read_buffer.consume(self->m_read_buffer.size());
                    if (auto handler = self->m_parent.get_event_handler()) {
                        ConnectionHandle ch(self->m_id);
                        handler->on_message(TransportMessage(ch, payload, is_binary));
                    }
                    self->do_read();
                }));
        }

        void do_write() {
            if (m_out_queue.empty()) return;
            auto msg = m_out_queue.front();
            m_ws.binary(msg.binary);
            auto self = shared_from_this();
            m_ws.async_write(boost::asio::buffer(msg.data), boost::asio::bind_executor(self->m_ws.get_executor(),
                [self](beast::error_code ec, std::size_t) {
                    if (ec) {
                        self->fail("write", ec);
                        return;
                    }
                    self->m_out_queue.pop_front();
                    if (!self->m_out_queue.empty()) self->do_write();
                }));
        }

        void fail(const char* what, beast::error_code ec) {
            LOG_WARN("Beast session ", m_id, " ", what, ": ", ec.message());
            if (auto handler = m_parent.get_event_handler()) {
                ConnectionHandle ch(m_id);
                handler->on_error(ch, std::string(what) + ": " + ec.message());
            }
        }

        void on_closed_internal(int code, const std::string& reason) {
            // Inform parent to cleanup maps and notify handler
            m_parent.on_session_close(m_id, code, reason);
        }

        static void parse_query_parameters(const std::string& resource,
                                           std::map<std::string, std::string>& params) {
            auto qpos = resource.find('?');
            if (qpos == std::string::npos) return;
            auto query = resource.substr(qpos + 1);
            size_t pos = 0;
            while (pos < query.size()) {
                auto amp = query.find('&', pos);
                auto part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
                auto eq = part.find('=');
                std::string key = (eq == std::string::npos) ? part : part.substr(0, eq);
                std::string value = (eq == std::string::npos) ? std::string() : part.substr(eq + 1);
                if (!key.empty()) params[key] = value;
                if (amp == std::string::npos) break;
                pos = amp + 1;
            }
        }

        WebSocketTransport& m_parent;
        websocket::stream<tcp::socket> m_ws;
        beast::flat_buffer m_buffer;                  // for initial HTTP read
        http::request<http::string_body> m_req;       // handshake request
    beast::flat_buffer m_read_buffer;             // for websocket messages
        std::deque<OutMsg> m_out_queue;
        ConnectionId m_id;
        bool m_is_upgrade{false};
    };

    void do_accept() {
        m_acceptor.async_accept(m_socket, [this](beast::error_code ec) {
            if (!ec) {
                std::make_shared<Session>(*this, std::move(m_socket))->run();
            } else {
                LOG_WARN("Accept error: ", ec.message());
            }
            // Continue accepting
            if (m_acceptor.is_open()) do_accept();
        });
    }

    void register_session(std::shared_ptr<Session> session, std::shared_ptr<ConnectionInfo> info) {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_sessions[session->id()] = session;
        m_connections[session->id()] = std::move(info);
    }

    void on_session_close(const ConnectionId& id, int code, const std::string& reason) {
        // Remove from maps and notify handler
        {
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            m_sessions.erase(id);
            m_connections.erase(id);
        }
        if (auto handler = get_event_handler()) {
            handler->on_connection_close(ConnectionHandle(id), code, reason);
        }
        LOG_DEBUG("WebSocket(Beast) connection closed: ", id);
    }

    std::string generate_connection_id() { return lib::uuid::uuid1(); }

    std::shared_ptr<TransportEventHandler> get_event_handler() {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        return m_event_handler;
    }

private:
    asio::io_service& m_io_service;
    tcp::acceptor m_acceptor;
    tcp::socket m_socket;

    mutable std::mutex m_handler_mutex;
    std::shared_ptr<TransportEventHandler> m_event_handler;

    mutable std::mutex m_connections_mutex;
    std::unordered_map<ConnectionId, std::shared_ptr<ConnectionInfo>> m_connections;
    std::unordered_map<ConnectionId, std::shared_ptr<Session>> m_sessions;
};

/**
 * @brief WebSocket transport factory
 */
class WebSocketTransportFactory : public TransportFactory {
public:
    std::unique_ptr<Transport> create_transport(asio::io_service& io_service) override {
        return std::make_unique<WebSocketTransport>(io_service);
    }

    std::string get_transport_type() const override { return "websocket"; }
};

} // namespace transport
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_WEBSOCKET_TRANSPORT_HPP
