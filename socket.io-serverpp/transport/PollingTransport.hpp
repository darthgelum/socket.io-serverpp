#ifndef SOCKETIO_SERVERPP_POLLING_TRANSPORT_HPP
#define SOCKETIO_SERVERPP_POLLING_TRANSPORT_HPP

#include "Transport.hpp"
#include "../config.hpp"
#include "../Logger.hpp"
#include "../uuid.hpp"
#include "../engineio/EngineIOSession.hpp" // for engineio::packet_type constants

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>

#include <deque>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include <map>
#include <chrono>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace transport {

namespace beast = boost::beast;     // from <boost/beast.hpp>
namespace http  = beast::http;      // from <boost/beast/http.hpp>
namespace net   = boost::asio;      // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;   // from <boost/asio/ip/tcp.hpp>

/**
 * @brief A minimal Engine.IO HTTP long-polling transport implemented with Boost.Beast.
 *
 * This implementation keeps things simple:
 * - Each initial GET without sid creates a transport connection (connection.id)
 *   and notifies the upper layer via on_connection_open.
 * - Outgoing packets are queued per connection.
 * - GET with sid responds immediately with all queued packets (or a NOOP if none).
 * - POST with sid decodes the payload (may contain multiple packets) and forwards
 *   them individually via on_message.
 * - No request is held open (short-poll), which is compatible albeit less efficient.
 */
class PollingTransport : public Transport, public std::enable_shared_from_this<PollingTransport> {
public:
    explicit PollingTransport(asio::io_service& io_service)
        : m_io_service(io_service)
        , m_acceptor(io_service)
        , m_socket(io_service) {}

    ~PollingTransport() override {
        stop();
    }

    void set_event_handler(std::shared_ptr<TransportEventHandler> handler) override {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        m_event_handler = handler;
    }

    void listen(const std::string& address, int port) override {
        boost::system::error_code ec;

        tcp::endpoint endpoint{net::ip::make_address(address, ec), static_cast<unsigned short>(port)};
        if (ec) {
            LOG_ERROR("PollingTransport invalid address: ", ec.message());
            throw SocketIOException("Invalid address for PollingTransport", SocketIOErrorCode::CONNECTION_FAILED);
        }

        m_acceptor.open(endpoint.protocol(), ec);
        if (ec) {
            LOG_ERROR("PollingTransport open failed: ", ec.message());
            throw SocketIOException("Failed to open acceptor", SocketIOErrorCode::CONNECTION_FAILED);
        }

        m_acceptor.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            LOG_WARN("PollingTransport set_option failed: ", ec.message());
        }

        m_acceptor.bind(endpoint, ec);
        if (ec) {
            LOG_ERROR("PollingTransport bind failed: ", ec.message());
            throw SocketIOException("Failed to bind PollingTransport", SocketIOErrorCode::CONNECTION_FAILED);
        }

        m_acceptor.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            LOG_ERROR("PollingTransport listen failed: ", ec.message());
            throw SocketIOException("Failed to listen PollingTransport", SocketIOErrorCode::CONNECTION_FAILED);
        }

        do_accept();
        LOG_INFO("Polling transport listening on ", address, ":", port);
    }

    void start_accept() override {
        // Nothing extra; accept loop is already running after listen().
        LOG_DEBUG("Polling transport accepting HTTP connections");
    }

    bool send_message(const ConnectionHandle& connection, const std::string& message, bool /*is_binary*/ = false) override {
        std::shared_ptr<tcp::socket> to_send_socket;
        std::shared_ptr<http::request<http::string_body>> to_send_req;
        std::string body_to_send;

        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            auto it = m_connections.find(connection.id);
            if (it == m_connections.end()) {
                LOG_WARN("PollingTransport send_message: unknown connection ", connection.id);
                return false;
            }

            // If this is an Engine.IO OPEN packet, parse sid and map sid->conn
            if (!message.empty() && message[0] == engine_io::OPEN) {
                // Expect JSON like {"sid":"<sid>"...}
                auto brace = message.find('{');
                if (brace != std::string::npos) {
                    auto json = message.substr(brace);
                    auto sid_pos = json.find("\"sid\"");
                    if (sid_pos != std::string::npos) {
                        auto colon = json.find(':', sid_pos);
                        auto quote1 = json.find('"', colon + 1);
                        auto quote2 = (quote1 != std::string::npos) ? json.find('"', quote1 + 1) : std::string::npos;
                        if (quote1 != std::string::npos && quote2 != std::string::npos && quote2 > quote1 + 1) {
                            std::string sid = json.substr(quote1 + 1, quote2 - (quote1 + 1));
                            m_sid_to_conn[sid] = connection.id;
                            LOG_DEBUG("PollingTransport mapped sid ", sid, " to conn ", connection.id);
                        }
                    }
                }
            }

            // If there's a pending GET waiting, fulfill it immediately with all queued + this message
            if (it->second.pending_socket && it->second.pending_req) {
                it->second.outgoing.emplace_back(message);
                body_to_send = encode_payload(it->second.outgoing);
                it->second.outgoing.clear();

                to_send_socket = std::move(it->second.pending_socket);
                to_send_req = std::move(it->second.pending_req);
                if (it->second.pending_timer) {
                    boost::system::error_code ec;
                    it->second.pending_timer->cancel(ec);
                }
                it->second.pending_timer.reset();
            } else {
                // No pending GET: enqueue for next GET
                it->second.outgoing.emplace_back(message);
            }
        }

        if (to_send_socket && to_send_req) {
            http::response<http::string_body> res{http::status::ok, to_send_req->version()};
            fill_cors_headers(res, *to_send_req);
            res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate");
            res.set(http::field::content_type, "text/plain; charset=UTF-8");
            res.body() = std::move(body_to_send);
            res.prepare_payload();
            write_and_close(to_send_socket, std::move(res));
        }

        return true;
    }

    void close_connection(const ConnectionHandle& connection, int /*code*/ = 1000, const std::string& reason = "Normal closure") override {
        std::shared_ptr<tcp::socket> pending;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            // Remove by connection id and any sids pointing to it
            for (auto it = m_sid_to_conn.begin(); it != m_sid_to_conn.end(); ) {
                if (it->second == connection.id) {
                    it = m_sid_to_conn.erase(it);
                } else {
                    ++it;
                }
            }
            auto itc = m_connections.find(connection.id);
            if (itc != m_connections.end()) {
                if (itc->second.pending_timer) {
                    boost::system::error_code ec;
                    itc->second.pending_timer->cancel(ec);
                }
                pending = std::move(itc->second.pending_socket);
                m_connections.erase(itc);
            }
        }
        if (pending) {
            boost::system::error_code ignore;
            pending->shutdown(tcp::socket::shutdown_both, ignore);
            pending->close(ignore);
        }
        LOG_DEBUG("PollingTransport closed connection: ", connection.id, " - ", reason);
    }

    void stop() override {
        boost::system::error_code ec;
        m_acceptor.close(ec);
        if (ec) {
            LOG_WARN("PollingTransport acceptor close: ", ec.message());
        }
        m_socket.close(ec);
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            for (auto& kv : m_connections) {
                if (kv.second.pending_timer) {
                    boost::system::error_code ec2;
                    kv.second.pending_timer->cancel(ec2);
                }
                if (kv.second.pending_socket) {
                    boost::system::error_code ignore2;
                    kv.second.pending_socket->shutdown(tcp::socket::shutdown_both, ignore2);
                    kv.second.pending_socket->close(ignore2);
                }
            }
            m_connections.clear();
            m_sid_to_conn.clear();
        }
        LOG_INFO("Polling transport stopped");
    }

    std::string get_name() const override { return "polling"; }
    bool supports_binary() const override { return false; }

    std::shared_ptr<ConnectionInfo> get_connection_info(const ConnectionHandle& connection) const override {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        auto it = m_connections.find(connection.id);
        if (it != m_connections.end()) return it->second.info;
        return nullptr;
    }

private:
    struct ConnState {
        std::shared_ptr<ConnectionInfo> info;
        std::deque<std::string> outgoing; // queued engine.io packets (already framed at packet-level)
    // Long-poll pending GET state
    std::shared_ptr<tcp::socket> pending_socket;
    std::shared_ptr<http::request<http::string_body>> pending_req;
    std::shared_ptr<asio::steady_timer> pending_timer;
    };

    // Timeout helper is defined below in-class

    void do_accept() {
        m_acceptor.async_accept(m_socket, [self = shared_from_this()](boost::system::error_code ec) {
            if (!ec) {
                self->handle_session(std::move(self->m_socket));
            } else {
                LOG_WARN("PollingTransport accept error: ", ec.message());
            }
            // Prepare for next accept
            self->do_accept();
        });
    }

    void handle_session(tcp::socket socket) {
        // Use shared_ptr to keep socket alive through async operations
        auto sp_socket = std::make_shared<tcp::socket>(std::move(socket));
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto req = std::make_shared<http::request<http::string_body>>();

        http::async_read(*sp_socket, *buffer, *req,
            [self = shared_from_this(), sp_socket, buffer, req](boost::system::error_code ec, std::size_t) mutable {
                if (ec) {
                    LOG_WARN("PollingTransport read error: ", ec.message());
                    beast::error_code ignore;
                    sp_socket->shutdown(tcp::socket::shutdown_send, ignore);
                    return;
                }

                self->dispatch_request(sp_socket, *req);
            });
    }

    static std::map<std::string, std::string> parse_query(const std::string& target) {
        std::map<std::string, std::string> out;
        auto pos_q = target.find('?');
        if (pos_q == std::string::npos) return out;
        auto q = target.substr(pos_q + 1);
        size_t pos = 0;
        while (pos < q.size()) {
            auto amp = q.find('&', pos);
            auto seg = q.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            auto eq = seg.find('=');
            std::string key = (eq == std::string::npos) ? seg : seg.substr(0, eq);
            std::string val = (eq == std::string::npos) ? std::string() : seg.substr(eq + 1);
            if (!key.empty()) out[key] = val;
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
        return out;
    }

    static std::string path_only(const std::string& target) {
        auto pos_q = target.find('?');
        return (pos_q == std::string::npos) ? target : target.substr(0, pos_q);
    }

    void dispatch_request(std::shared_ptr<tcp::socket> socket, const http::request<http::string_body>& req) {
        // Very basic CORS/OPTIONS handling
        if (req.method() == http::verb::options) {
            http::response<http::string_body> res{http::status::no_content, req.version()};
            fill_cors_headers(res, req);
            res.prepare_payload();
            write_and_close(socket, std::move(res));
            return;
        }

        auto target_path = path_only(std::string(req.target()));
        auto qparams = parse_query(std::string(req.target()));

        // Require transport=polling
        auto tp = qparams.find("transport");
        if (tp == qparams.end() || tp->second != "polling") {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            fill_cors_headers(res, req);
            res.set(http::field::content_type, "text/plain; charset=UTF-8");
            res.body() = "transport must be polling";
            res.prepare_payload();
            write_and_close(std::move(socket), std::move(res));
            return;
        }

        if (req.method() == http::verb::get) {
        handle_get(socket, req, qparams);
        } else if (req.method() == http::verb::post) {
        handle_post(socket, req, qparams);
        } else {
            http::response<http::string_body> res{http::status::method_not_allowed, req.version()};
            fill_cors_headers(res, req);
            res.prepare_payload();
        write_and_close(socket, std::move(res));
        }
    }

    void handle_get(std::shared_ptr<tcp::socket> socket, const http::request<http::string_body>& req,
                    const std::map<std::string, std::string>& qparams) {
        std::string sid;
        if (auto it = qparams.find("sid"); it != qparams.end()) sid = it->second;

        ConnectionId conn_id;
        bool is_new = false;

        if (sid.empty()) {
            // New connection: create conn_id and notify handler
            conn_id = lib::uuid::uuid1();

            auto info = std::make_shared<ConnectionInfo>(conn_id);
            boost::system::error_code ecrem;
            info->remote_address = socket->remote_endpoint(ecrem).address().to_string();
            info->user_agent = std::string(req[http::field::user_agent]);
            info->query_params = qparams;

            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                ConnState state; state.info = info;
                m_connections.emplace(conn_id, std::move(state));
            }

            // Notify engine.io synchronously so handshake is queued
            if (auto handler = get_event_handler()) {
                handler->on_connection_open(*info);
            }
            is_new = true;
        } else {
            // Existing connection: map sid -> conn_id
            std::lock_guard<std::mutex> lock(m_state_mutex);
            auto it_sid = m_sid_to_conn.find(sid);
            if (it_sid == m_sid_to_conn.end()) {
                http::response<http::string_body> res{http::status::bad_request, req.version()};
                fill_cors_headers(res, req);
                res.set(http::field::content_type, "text/plain; charset=UTF-8");
                res.body() = "unknown sid";
                res.prepare_payload();
                write_and_close(socket, std::move(res));
                return;
            }
            conn_id = it_sid->second;
        }

        // Build response body: on initial GET (no sid), return raw OPEN packet without payload framing
        if (is_new) {
            std::string body;
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                auto it = m_connections.find(conn_id);
                if (it != m_connections.end() && !it->second.outgoing.empty()) {
                    body = std::move(it->second.outgoing.front());
                    it->second.outgoing.pop_front();
                }
            }

            http::response<http::string_body> res{http::status::ok, req.version()};
            fill_cors_headers(res, req);
            res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate");
            res.set(http::field::content_type, "text/plain; charset=UTF-8");
            res.body() = std::move(body);
            res.prepare_payload();
            write_and_close(socket, std::move(res));
            LOG_DEBUG("PollingTransport new connection GET served, conn_id set");
            return;
        }

        // Existing session: flush queued immediately if any; otherwise hold as long-poll until data or timeout
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            auto it = m_connections.find(conn_id);
            if (it == m_connections.end()) {
                http::response<http::string_body> res{http::status::bad_request, req.version()};
                fill_cors_headers(res, req);
                res.set(http::field::content_type, "text/plain; charset=UTF-8");
                res.body() = "unknown connection";
                res.prepare_payload();
                write_and_close(socket, std::move(res));
                return;
            }

            if (!it->second.outgoing.empty()) {
                std::string body = encode_payload(it->second.outgoing);
                it->second.outgoing.clear();
                http::response<http::string_body> res{http::status::ok, req.version()};
                fill_cors_headers(res, req);
                res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate");
                res.set(http::field::content_type, "text/plain; charset=UTF-8");
                res.body() = std::move(body);
                res.prepare_payload();
                write_and_close(socket, std::move(res));
                return;
            }

            // Hold request for long-polling
            it->second.pending_socket = socket;
            it->second.pending_req = std::make_shared<http::request<http::string_body>>(req);
            it->second.pending_timer = std::make_shared<asio::steady_timer>(m_io_service);
            auto timer = it->second.pending_timer;
            timer->expires_after(std::chrono::milliseconds(timeouts::DEFAULT_PING_INTERVAL));
            auto held_conn = conn_id;
            timer->async_wait([self = shared_from_this(), held_conn](const boost::system::error_code& ec){
                if (ec) return; // cancelled
                self->on_pending_timeout(held_conn);
            });
            LOG_TRACE("PollingTransport holding GET (long-poll) for conn ", conn_id);
        }
    }

    void handle_post(std::shared_ptr<tcp::socket> socket, const http::request<http::string_body>& req,
                     const std::map<std::string, std::string>& qparams) {
        std::string sid;
        if (auto it = qparams.find("sid"); it != qparams.end()) sid = it->second;

        if (sid.empty()) {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            fill_cors_headers(res, req);
            res.set(http::field::content_type, "text/plain; charset=UTF-8");
            res.body() = "sid required";
            res.prepare_payload();
            write_and_close(socket, std::move(res));
            return;
        }

        ConnectionId conn_id;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            auto it_sid = m_sid_to_conn.find(sid);
            if (it_sid == m_sid_to_conn.end()) {
                http::response<http::string_body> res{http::status::bad_request, req.version()};
                fill_cors_headers(res, req);
                res.set(http::field::content_type, "text/plain; charset=UTF-8");
                res.body() = "unknown sid";
                res.prepare_payload();
                write_and_close(socket, std::move(res));
                return;
            }
            conn_id = it_sid->second;
        }

        // Decode payload: may be one or multiple length-prefixed packets
        auto packets = decode_payload(req.body());
        auto handler = get_event_handler();
        if (handler) {
            for (auto& pkt : packets) {
                LOG_TRACE("PollingTransport POST packet len=", pkt.size());
                handler->on_message(TransportMessage(ConnectionHandle(conn_id), pkt, false));
            }
        }

        // Respond with ok
    http::response<http::string_body> res{http::status::ok, req.version()};
        fill_cors_headers(res, req);
        res.set(http::field::content_type, "text/plain; charset=UTF-8");
        res.body() = "ok";
        res.prepare_payload();
    write_and_close(socket, std::move(res));
    }

    static std::string encode_payload(const std::deque<std::string>& packets) {
        // Engine.IO v4: use ASCII RS (0x1e) to delimit multiple packets
        constexpr char RS = '\x1e';
        if (packets.empty()) return {};
        if (packets.size() == 1) return packets.front();
        std::string out;
        size_t total = 0;
        for (auto& p : packets) total += p.size() + 1; // +1 for RS
        out.reserve(total);
        bool first = true;
        for (auto& p : packets) {
            if (!first) out.push_back(RS);
            out += p;
            first = false;
        }
        return out;
    }

    static std::vector<std::string> decode_payload(const std::string& body) {
        // Engine.IO v4: packets separated by ASCII RS (0x1e).
        // If no RS present, body is a single packet.
        std::vector<std::string> out;
        if (body.empty()) return out;
        constexpr char RS = '\x1e';
        size_t start = 0;
        size_t pos;
        while ((pos = body.find(RS, start)) != std::string::npos) {
            out.emplace_back(body.substr(start, pos - start));
            start = pos + 1;
        }
        out.emplace_back(body.substr(start));
        return out;
    }

    template <class Res>
    void write_and_close(std::shared_ptr<tcp::socket> socket, Res&& res) {
        auto sp = std::make_shared<Res>(std::forward<Res>(res));
        http::async_write(*socket, *sp, [socket, sp](boost::system::error_code ec, std::size_t) mutable {
            beast::error_code ignore;
            socket->shutdown(tcp::socket::shutdown_send, ignore);
            socket->close(ignore);
            if (ec) {
                LOG_WARN("PollingTransport write error: ", ec.message());
            }
        });
    }

    template <class ResOrReq>
    static void fill_cors_headers(ResOrReq& msg, const http::request<http::string_body>& req) {
        // Very relaxed CORS for development
        auto origin = req[http::field::origin];
        msg.set(http::field::access_control_allow_origin, origin.empty() ? "*" : origin);
        msg.set(http::field::access_control_allow_credentials, "true");
        msg.set(http::field::access_control_allow_headers, "Content-Type, Authorization, X-Requested-With");
        msg.set(http::field::access_control_allow_methods, "GET,POST,OPTIONS");
    }

    std::shared_ptr<TransportEventHandler> get_event_handler() {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        return m_event_handler;
    }

    // Timeout handler for a held long-poll GET: respond with queued data or NOOP
    void on_pending_timeout(const ConnectionId& conn_id) {
        std::shared_ptr<tcp::socket> socket;
        std::shared_ptr<http::request<http::string_body>> req;
        std::string body;

        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            auto it = m_connections.find(conn_id);
            if (it == m_connections.end()) return;

            if (!it->second.outgoing.empty()) {
                body = encode_payload(it->second.outgoing);
                it->second.outgoing.clear();
            } else {
                std::deque<std::string> noop;
                noop.emplace_back(1, engineio::packet_type::NOOP);
                body = encode_payload(noop);
            }

            socket = std::move(it->second.pending_socket);
            req = std::move(it->second.pending_req);
            if (it->second.pending_timer) {
                boost::system::error_code ec;
                it->second.pending_timer->cancel(ec);
            }
            it->second.pending_timer.reset();
        }

        if (socket && req) {
            http::response<http::string_body> res{http::status::ok, req->version()};
            fill_cors_headers(res, *req);
            res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate");
            res.set(http::field::content_type, "text/plain; charset=UTF-8");
            res.body() = std::move(body);
            res.prepare_payload();
            write_and_close(socket, std::move(res));
            LOG_TRACE("PollingTransport long-poll timeout responded for conn ", conn_id);
        }
    }

private:
    asio::io_service& m_io_service;
    tcp::acceptor m_acceptor;
    tcp::socket m_socket;

    mutable std::mutex m_handler_mutex;
    std::shared_ptr<TransportEventHandler> m_event_handler;

    mutable std::mutex m_state_mutex;
    std::unordered_map<ConnectionId, ConnState> m_connections;
    std::unordered_map<std::string, ConnectionId> m_sid_to_conn;
};

/**
 * @brief Factory for PollingTransport
 */
class PollingTransportFactory : public TransportFactory {
public:
    std::unique_ptr<Transport> create_transport(asio::io_service& io_service) override {
        return std::make_unique<PollingTransport>(io_service);
    }

    std::string get_transport_type() const override { return "polling"; }
};

} // namespace transport
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_POLLING_TRANSPORT_HPP
