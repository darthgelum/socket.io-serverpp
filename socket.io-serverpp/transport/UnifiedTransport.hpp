#ifndef SOCKETIO_SERVERPP_UNIFIED_TRANSPORT_HPP
#define SOCKETIO_SERVERPP_UNIFIED_TRANSPORT_HPP

#include "Transport.hpp"
#include "../config.hpp"
#include "../Logger.hpp"
#include "../uuid.hpp"
#include "../engineio/EngineIOSession.hpp" // for engineio::packet_type constants

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace transport {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

/**
 * @brief Unified transport that handles Engine.IO HTTP polling and WebSocket upgrade on a single port.
 */
class UnifiedTransport : public Transport, public std::enable_shared_from_this<UnifiedTransport> {
public:
    explicit UnifiedTransport(asio::io_service& io_service)
        : m_io_service(io_service), m_acceptor(io_service), m_socket(io_service) {}

    ~UnifiedTransport() override { stop(); }

    void set_event_handler(std::shared_ptr<TransportEventHandler> handler) override {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        m_event_handler = handler;
    }

    void listen(const std::string& address, int port) override {
        boost::system::error_code ec;
        tcp::endpoint endpoint{boost::asio::ip::make_address(address, ec), static_cast<unsigned short>(port)};
        if (ec) {
            LOG_ERROR("UnifiedTransport invalid address: ", ec.message());
            throw SocketIOException("Invalid address", SocketIOErrorCode::CONNECTION_FAILED);
        }
        m_acceptor.open(endpoint.protocol(), ec);
        if (ec) {
            LOG_ERROR("UnifiedTransport open failed: ", ec.message());
            throw SocketIOException("Failed to open acceptor", SocketIOErrorCode::CONNECTION_FAILED);
        }
        m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec) LOG_WARN("UnifiedTransport set_option failed: ", ec.message());
        m_acceptor.bind(endpoint, ec);
        if (ec) {
            LOG_ERROR("UnifiedTransport bind failed: ", ec.message());
            throw SocketIOException("Failed to bind", SocketIOErrorCode::CONNECTION_FAILED);
        }
        m_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) {
            LOG_ERROR("UnifiedTransport listen failed: ", ec.message());
            throw SocketIOException("Failed to listen", SocketIOErrorCode::CONNECTION_FAILED);
        }
        LOG_INFO("Unified transport listening on ", address, ":", port);
    }

    void start_accept() override {
        do_accept();
        LOG_DEBUG("Unified transport started accepting connections");
    }

    bool send_message(const ConnectionHandle& connection, const std::string& message, bool is_binary = false) override {
        // Try WebSocket session first
        std::shared_ptr<class WSSession> ws;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_ws_sessions.find(connection.id);
            if (it != m_ws_sessions.end()) ws = it->second;
        }
        if (ws) {
            LOG_TRACE("UnifiedTransport send -> WS conn=", connection.id, ", bytes=", message.size());
            // If this is Engine.IO OPEN, map sid->conn for completeness
            if (!message.empty() && message[0] == engineio::packet_type::OPEN) {
                auto brace = message.find('{');
                if (brace != std::string::npos) {
                    auto json = message.substr(brace);
                    auto sid_pos = json.find("\"sid\"");
                    if (sid_pos != std::string::npos) {
                        auto colon = json.find(':', sid_pos);
                        auto q1 = json.find('"', colon + 1);
                        auto q2 = (q1 != std::string::npos) ? json.find('"', q1 + 1) : std::string::npos;
                        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
                            std::string sid = json.substr(q1 + 1, q2 - (q1 + 1));
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_sid_to_conn[sid] = connection.id;
                            LOG_DEBUG("UnifiedTransport(WS) mapped sid ", sid, " to conn ", connection.id);
                        }
                    }
                }
            }
            ws->send(message, is_binary);
            return true;
        }

        // Otherwise, treat as polling
        std::shared_ptr<tcp::socket> to_send_socket;
        std::shared_ptr<http::request<http::string_body>> to_send_req;
        std::string body_to_send;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_polling.find(connection.id);
            if (it == m_polling.end()) {
                LOG_WARN("UnifiedTransport send: unknown connection ", connection.id);
                return false;
            }

            // If this is an Engine.IO OPEN packet, parse sid and map sid->conn
            if (!message.empty() && message[0] == engineio::packet_type::OPEN) {
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
                            LOG_DEBUG("UnifiedTransport mapped sid ", sid, " to conn ", connection.id);
                        }
                    }
                }
            }

            // If a GET is pending, flush immediately
            auto& st = it->second;
            if (st.pending_socket && st.pending_req) {
                st.outgoing.emplace_back(message);
                body_to_send = encode_payload(st.outgoing);
                st.outgoing.clear();
                to_send_socket = std::move(st.pending_socket);
                to_send_req = std::move(st.pending_req);
                if (st.pending_timer) {
                    boost::system::error_code ec;
                    st.pending_timer->cancel(ec);
                }
                st.pending_timer.reset();
            } else {
                st.outgoing.emplace_back(message);
                LOG_TRACE("UnifiedTransport queued polling msg for conn=", connection.id, ", queue=", st.outgoing.size());
            }
        }

        if (to_send_socket && to_send_req) {
            http::response<http::string_body> res{http::status::ok, to_send_req->version()};
            fill_cors_headers(res, *to_send_req);
            res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate");
            res.set(http::field::content_type, "text/plain; charset=UTF-8");
            res.body() = std::move(body_to_send);
            res.prepare_payload();
            LOG_TRACE("UnifiedTransport flush polling response for conn=", connection.id, ", bytes=", res.body().size());
            write_and_close(to_send_socket, std::move(res));
        }
        return true;
    }

    void close_connection(const ConnectionHandle& connection, int code = 1000, const std::string& reason = "Normal closure") override {
        // WebSocket path
        std::shared_ptr<class WSSession> ws;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_ws_sessions.find(connection.id);
            if (it != m_ws_sessions.end()) ws = it->second;
        }
    if (ws) {
            ws->close(code, reason);
            return;
        }

        // Polling path
        std::shared_ptr<tcp::socket> pending;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_sid_to_conn.begin(); it != m_sid_to_conn.end();) {
                if (it->second == connection.id) it = m_sid_to_conn.erase(it); else ++it;
            }
            auto itc = m_polling.find(connection.id);
            if (itc != m_polling.end()) {
                if (itc->second.pending_timer) {
                    boost::system::error_code ec; itc->second.pending_timer->cancel(ec);
                }
                pending = std::move(itc->second.pending_socket);
                m_polling.erase(itc);
            }
        }
        if (pending) {
            boost::system::error_code ignore;
            pending->shutdown(tcp::socket::shutdown_both, ignore);
            pending->close(ignore);
        }
        LOG_DEBUG("UnifiedTransport closed connection: ", connection.id, " - ", reason);
    }

    void stop() override {
        boost::system::error_code ec;
        m_acceptor.close(ec);
        m_socket.close(ec);
        std::unordered_map<ConnectionId, std::shared_ptr<WSSession>> ws_copy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ws_copy = m_ws_sessions;
        }
        for (auto& kv : ws_copy) if (kv.second) kv.second->close(static_cast<int>(websocket::close_code::going_away), "Server shutdown");

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ws_sessions.clear();
            m_polling.clear();
            m_sid_to_conn.clear();
        }
        LOG_INFO("Unified transport stopped");
    }

    std::string get_name() const override { return "unified"; }
    bool supports_binary() const override { return true; }

    std::shared_ptr<ConnectionInfo> get_connection_info(const ConnectionHandle& connection) const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_conn_info.find(connection.id);
        if (it != m_conn_info.end()) return it->second;
        return nullptr;
    }

private:
    struct PollState {
        std::shared_ptr<ConnectionInfo> info;
        std::deque<std::string> outgoing;
        std::shared_ptr<tcp::socket> pending_socket;
        std::shared_ptr<http::request<http::string_body>> pending_req;
        std::shared_ptr<asio::steady_timer> pending_timer;
    };

    class WSSession : public std::enable_shared_from_this<WSSession> {
    public:
        struct Out { std::string data; bool binary; };
        WSSession(UnifiedTransport& parent, tcp::socket socket)
            : m_parent(parent), m_ws(std::move(socket)) {}

        const ConnectionId& id() const { return m_id; }

        void run(http::request<http::string_body> req, std::shared_ptr<ConnectionInfo> info, bool is_upgrade) {
            m_id = info->id; m_info = info; m_is_upgrade = is_upgrade;
            // Accept
            m_ws.set_option(websocket::stream_base::decorator([](websocket::response_type& res) {
                res.set(http::field::server, std::string("socket.io-serverpp/unified"));
            }));
            auto self = shared_from_this();
            m_ws.async_accept(req, [self](beast::error_code ec){
                if (ec) { self->fail("accept", ec); self->on_closed(1002, "Handshake failed"); return; }
                self->on_accepted();
            });
        }

        void send(const std::string& data, bool binary) {
            auto self = shared_from_this();
            boost::asio::post(self->m_ws.get_executor(), [self, data, binary]{
                bool writing = !self->m_q.empty();
                self->m_q.push_back(Out{data, binary});
                if (!writing) self->do_write();
            });
        }

        void close(int code, const std::string& reason) {
            websocket::close_reason cr; cr.code = static_cast<websocket::close_code>(code); cr.reason = reason;
            auto self = shared_from_this();
            boost::asio::post(self->m_ws.get_executor(), [self, cr]{
                self->m_ws.async_close(cr, [self](beast::error_code ec){ if (ec) self->fail("close", ec); });
            });
        }

    private:
        void on_accepted() {
            if (!m_is_upgrade) {
                if (auto h = m_parent.get_event_handler()) h->on_connection_open(*m_info);
            }
            do_read();
        }

        void do_read() {
            auto self = shared_from_this();
            m_ws.async_read(m_read_buf, [self](beast::error_code ec, std::size_t){
                if (ec) { if (ec == websocket::error::closed) { self->on_closed(1000, "WebSocket closed"); return; }
                          self->fail("read", ec); self->on_closed(1006, ec.message()); return; }
                bool is_bin = self->m_ws.got_binary();
                std::string payload = beast::buffers_to_string(self->m_read_buf.data());
                self->m_read_buf.consume(self->m_read_buf.size());
                if (auto h = self->m_parent.get_event_handler()) h->on_message(TransportMessage(ConnectionHandle(self->m_id), payload, is_bin));
                self->do_read();
            });
        }

        void do_write() {
            auto msg = m_q.front(); m_ws.binary(msg.binary);
            auto self = shared_from_this();
            m_ws.async_write(boost::asio::buffer(msg.data), [self](beast::error_code ec, std::size_t){
                if (ec) { self->fail("write", ec); return; }
                self->m_q.pop_front();
                if (!self->m_q.empty()) self->do_write();
            });
        }

        void fail(const char* what, beast::error_code ec) {
            LOG_WARN("Unified WS session ", m_id, " ", what, ": ", ec.message());
            if (auto h = m_parent.get_event_handler()) h->on_error(ConnectionHandle(m_id), std::string(what) + ": " + ec.message());
        }

        void on_closed(int code, const std::string& reason) { m_parent.on_ws_close(m_id, code, reason); }

        UnifiedTransport& m_parent;
        websocket::stream<tcp::socket> m_ws;
        beast::flat_buffer m_read_buf;
        std::deque<Out> m_q;
        std::shared_ptr<ConnectionInfo> m_info;
        ConnectionId m_id;
        bool m_is_upgrade{false};
    };

    void do_accept() {
        m_acceptor.async_accept(m_socket, [self = shared_from_this()](beast::error_code ec){
            if (!ec) self->handle_connection(std::move(self->m_socket));
            else LOG_WARN("Unified accept error: ", ec.message());
            if (self->m_acceptor.is_open()) self->do_accept();
        });
    }

    void handle_connection(tcp::socket socket) {
        auto sp_socket = std::make_shared<tcp::socket>(std::move(socket));
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto req = std::make_shared<http::request<http::string_body>>();
        http::async_read(*sp_socket, *buffer, *req, [self = shared_from_this(), sp_socket, buffer, req](boost::system::error_code ec, std::size_t) mutable {
            if (ec) { LOG_WARN("Unified read error: ", ec.message()); beast::error_code ig; sp_socket->shutdown(tcp::socket::shutdown_send, ig); return; }
            self->dispatch_request(sp_socket, *req);
        });
    }

    static std::map<std::string, std::string> parse_query(const std::string& target) {
        std::map<std::string, std::string> out; auto pos_q = target.find('?'); if (pos_q == std::string::npos) return out;
        auto q = target.substr(pos_q + 1); size_t pos = 0; while (pos < q.size()) { auto amp = q.find('&', pos); auto seg = q.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos); auto eq = seg.find('='); std::string key = (eq == std::string::npos) ? seg : seg.substr(0, eq); std::string val = (eq == std::string::npos) ? std::string() : seg.substr(eq + 1); if (!key.empty()) out[key] = val; if (amp == std::string::npos) break; pos = amp + 1; } return out;
    }

    void dispatch_request(std::shared_ptr<tcp::socket> socket, const http::request<http::string_body>& req) {
        // OPTIONS/CORS
        if (req.method() == http::verb::options) {
            http::response<http::string_body> res{http::status::no_content, req.version()};
            fill_cors_headers(res, req); res.prepare_payload(); write_and_close(socket, std::move(res)); return;
        }

        // WebSocket upgrade?
        if (websocket::is_upgrade(req)) {
            handle_websocket(std::move(*socket), req);
            return;
        }

        // Otherwise HTTP polling
        handle_polling(socket, req);
    }

    void handle_websocket(tcp::socket socket, const http::request<http::string_body>& req) {
    auto qparams = parse_query(std::string(req.target()));
    LOG_DEBUG("Unified WS request target=", std::string(req.target()));
        std::string sid; if (auto it = qparams.find("sid"); it != qparams.end()) sid = it->second;

        ConnectionId conn_id;
        bool is_upgrade = false;
        if (!sid.empty()) {
            // Reuse existing connection id mapped for this sid (created during polling handshake)
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_sid_to_conn.find(sid);
            if (it == m_sid_to_conn.end()) {
                // Unknown sid: reject
                http::response<http::string_body> res{http::status::bad_request, req.version()};
                auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
                // Need a socket to write; since we only have a moved socket, close it immediately
                boost::system::error_code ig;
                socket.shutdown(tcp::socket::shutdown_both, ig);
                socket.close(ig);
                LOG_WARN("UnifiedTransport WS upgrade with unknown sid: ", sid);
                return;
            }
            conn_id = it->second;
            is_upgrade = true;
        } else {
            conn_id = lib::uuid::uuid1();
        }

        auto info = std::make_shared<ConnectionInfo>(conn_id);
        try { auto ep = socket.remote_endpoint(); info->remote_address = ep.address().to_string(); }
        catch (...) { info->remote_address = ""; }
        info->user_agent = std::string(req[http::field::user_agent]);
        info->query_params = qparams;
        for (auto const& h : req) info->headers[std::string(h.name_string())] = std::string(h.value());

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_conn_info[conn_id] = info;
        }

    auto session = std::make_shared<WSSession>(*this, std::move(socket));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ws_sessions[conn_id] = session;
            // If upgrading from polling, close any pending long-poll request
            if (is_upgrade) {
                auto itp = m_polling.find(conn_id);
                if (itp != m_polling.end()) {
                    if (itp->second.pending_timer) { boost::system::error_code ec; itp->second.pending_timer->cancel(ec); }
                    if (itp->second.pending_socket) {
                        boost::system::error_code ig;
                        itp->second.pending_socket->shutdown(tcp::socket::shutdown_both, ig);
                        itp->second.pending_socket->close(ig);
                    }
                    itp->second.pending_socket.reset();
                    itp->second.pending_req.reset();
                    itp->second.pending_timer.reset();
                }
                // Also ensure sid maps to this WebSocket connection id going forward
                if (!sid.empty()) {
                    auto it_sid = m_sid_to_conn.find(sid);
                    if (it_sid == m_sid_to_conn.end() || it_sid->second != conn_id) {
                        m_sid_to_conn[sid] = conn_id;
                    }
                }
            }
        }
    LOG_DEBUG("Unified WS accepted pre-accept conn_id=", conn_id, ", sid=", sid, ", upgrade=", is_upgrade);
    session->run(req, info, is_upgrade);
    }

    void handle_polling(std::shared_ptr<tcp::socket> socket, const http::request<http::string_body>& req) {
        auto target = std::string(req.target());
        auto qparams = parse_query(target);
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
            fill_cors_headers(res, req); res.prepare_payload(); write_and_close(socket, std::move(res));
        }
    }

    void handle_get(std::shared_ptr<tcp::socket> socket, const http::request<http::string_body>& req, const std::map<std::string, std::string>& qparams) {
        std::string sid; if (auto it = qparams.find("sid"); it != qparams.end()) sid = it->second;
        ConnectionId conn_id; bool is_new = false;
        if (sid.empty()) {
            conn_id = lib::uuid::uuid1();
            auto info = std::make_shared<ConnectionInfo>(conn_id);
            boost::system::error_code ecrem; info->remote_address = socket->remote_endpoint(ecrem).address().to_string();
            info->user_agent = std::string(req[http::field::user_agent]); info->query_params = qparams;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                // Initialize poll state and hold this GET as pending so the handshake flushes into it
                PollState st; st.info = info; 
                st.pending_socket = socket;
                st.pending_req = std::make_shared<http::request<http::string_body>>(req);
                m_polling.emplace(conn_id, std::move(st)); 
                m_conn_info[conn_id] = info;
            }
            LOG_DEBUG("Unified polling new connection conn_id=", conn_id);
            if (auto h = get_event_handler()) h->on_connection_open(*info);
            is_new = true;
        } else {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it_sid = m_sid_to_conn.find(sid);
            if (it_sid == m_sid_to_conn.end()) {
                http::response<http::string_body> res{http::status::bad_request, req.version()};
                fill_cors_headers(res, req); res.set(http::field::content_type, "text/plain; charset=UTF-8"); res.body() = "unknown sid"; res.prepare_payload(); write_and_close(socket, std::move(res)); return;
            }
            conn_id = it_sid->second;
        }

        if (is_new) {
            // The initial GET is being held pending; the Engine.IO handshake sent by on_connection_open
            // will flush directly into this pending response via send_message().
            return;
        }

        // Existing: flush queued packets if any (e.g., OPEN handshake), otherwise
        // if WS is already active, answer immediately with a NOOP; else hold.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_polling.find(conn_id);
            if (it == m_polling.end()) {
                http::response<http::string_body> res{http::status::bad_request, req.version()};
                fill_cors_headers(res, req); res.set(http::field::content_type, "text/plain; charset=UTF-8"); res.body() = "unknown connection"; res.prepare_payload(); write_and_close(socket, std::move(res)); return;
            }
            if (!it->second.outgoing.empty()) {
                std::string body = encode_payload(it->second.outgoing); it->second.outgoing.clear();
                http::response<http::string_body> res{http::status::ok, req.version()}; fill_cors_headers(res, req);
                res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate"); res.set(http::field::content_type, "text/plain; charset=UTF-8"); res.body() = std::move(body); res.prepare_payload(); write_and_close(socket, std::move(res)); return;
            }
            // If WebSocket is already active for this connection and there's nothing to flush,
            // answer immediately with a NOOP instead of holding the long-poll open.
            bool ws_active = (m_ws_sessions.find(conn_id) != m_ws_sessions.end());
            if (ws_active) {
                std::deque<std::string> noop; noop.emplace_back(1, engineio::packet_type::NOOP);
                http::response<http::string_body> res{http::status::ok, req.version()};
                fill_cors_headers(res, req);
                res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate");
                res.set(http::field::content_type, "text/plain; charset=UTF-8");
                res.body() = encode_payload(noop);
                res.prepare_payload();
                write_and_close(socket, std::move(res));
                LOG_TRACE("Unified polling GET answered immediately with NOOP (WS active, empty queue) for conn=", conn_id);
                return;
            }
            // If a previous pending socket exists but got closed, drop it
            if (it->second.pending_socket && !it->second.pending_socket->is_open()) {
                it->second.pending_socket.reset();
                it->second.pending_req.reset();
                if (it->second.pending_timer) { boost::system::error_code ec; it->second.pending_timer->cancel(ec); }
                it->second.pending_timer.reset();
            }
            it->second.pending_socket = socket;
            it->second.pending_req = std::make_shared<http::request<http::string_body>>(req);
            it->second.pending_timer = std::make_shared<asio::steady_timer>(m_io_service);
            auto timer = it->second.pending_timer; auto held_conn = conn_id;
            timer->expires_after(std::chrono::milliseconds(timeouts::DEFAULT_PING_INTERVAL));
            timer->async_wait([self = shared_from_this(), held_conn](const boost::system::error_code& ec){ if (ec) return; self->on_pending_timeout(held_conn); });
        }
    }

    void handle_post(std::shared_ptr<tcp::socket> socket, const http::request<http::string_body>& req, const std::map<std::string, std::string>& qparams) {
        std::string sid; if (auto it = qparams.find("sid"); it != qparams.end()) sid = it->second;
        if (sid.empty()) {
            http::response<http::string_body> res{http::status::bad_request, req.version()}; fill_cors_headers(res, req);
            res.set(http::field::content_type, "text/plain; charset=UTF-8"); res.body() = "sid required"; res.prepare_payload(); write_and_close(socket, std::move(res)); return;
        }
        ConnectionId conn_id;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it_sid = m_sid_to_conn.find(sid);
            if (it_sid == m_sid_to_conn.end()) {
                http::response<http::string_body> res{http::status::bad_request, req.version()}; fill_cors_headers(res, req);
                res.set(http::field::content_type, "text/plain; charset=UTF-8"); res.body() = "unknown sid"; res.prepare_payload(); write_and_close(socket, std::move(res)); return;
            }
            conn_id = it_sid->second;
        }
        auto packets = decode_payload(req.body());
        if (auto h = get_event_handler()) { for (auto& pkt : packets) h->on_message(TransportMessage(ConnectionHandle(conn_id), pkt, false)); }
        http::response<http::string_body> res{http::status::ok, req.version()}; fill_cors_headers(res, req); res.set(http::field::content_type, "text/plain; charset=UTF-8"); res.body() = "ok"; res.prepare_payload(); write_and_close(socket, std::move(res));
    }

    // Helpers reused from polling
    static std::string encode_payload(const std::deque<std::string>& packets) {
        constexpr char RS = '\x1e'; if (packets.empty()) return {}; if (packets.size() == 1) return packets.front();
        std::string out; size_t total = 0; for (auto& p : packets) total += p.size() + 1; out.reserve(total);
        bool first = true; for (auto& p : packets) { if (!first) out.push_back(RS); out += p; first = false; } return out;
    }
    static std::vector<std::string> decode_payload(const std::string& body) {
        std::vector<std::string> out; if (body.empty()) return out; constexpr char RS = '\x1e'; size_t start = 0; size_t pos; while ((pos = body.find(RS, start)) != std::string::npos) { out.emplace_back(body.substr(start, pos - start)); start = pos + 1; } out.emplace_back(body.substr(start)); return out;
    }
    template <class Res> void write_and_close(std::shared_ptr<tcp::socket> socket, Res&& res) {
        auto sp = std::make_shared<Res>(std::forward<Res>(res));
        http::async_write(*socket, *sp, [socket, sp](boost::system::error_code ec, std::size_t){ beast::error_code ig; socket->shutdown(tcp::socket::shutdown_send, ig); socket->close(ig); if (ec) LOG_WARN("Unified write error: ", ec.message()); });
    }
    template <class ResOrReq> static void fill_cors_headers(ResOrReq& msg, const http::request<http::string_body>& req) {
        auto origin = req[http::field::origin]; msg.set(http::field::access_control_allow_origin, origin.empty() ? "*" : origin);
        msg.set(http::field::access_control_allow_credentials, "true");
        msg.set(http::field::access_control_allow_headers, "Content-Type, Authorization, X-Requested-With");
        msg.set(http::field::access_control_allow_methods, "GET,POST,OPTIONS");
    }

    std::shared_ptr<TransportEventHandler> get_event_handler() {
        std::lock_guard<std::mutex> lock(m_handler_mutex); return m_event_handler;
    }

    void on_ws_close(const ConnectionId& id, int code, const std::string& reason) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ws_sessions.erase(id);
            m_conn_info.erase(id);
            // Remove any sid mapping pointing to this connection
            for (auto it = m_sid_to_conn.begin(); it != m_sid_to_conn.end(); ) {
                if (it->second == id) it = m_sid_to_conn.erase(it); else ++it;
            }
        }
        if (auto h = get_event_handler()) h->on_connection_close(ConnectionHandle(id), code, reason);
        LOG_DEBUG("Unified WS connection closed: ", id);
    }

    void on_pending_timeout(const ConnectionId& conn_id) {
        std::shared_ptr<tcp::socket> socket; std::shared_ptr<http::request<http::string_body>> req; std::string body;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_polling.find(conn_id); if (it == m_polling.end()) return;
            if (!it->second.outgoing.empty()) { body = encode_payload(it->second.outgoing); it->second.outgoing.clear(); }
            else { std::deque<std::string> noop; noop.emplace_back(1, engineio::packet_type::NOOP); body = encode_payload(noop); }
            socket = std::move(it->second.pending_socket); req = std::move(it->second.pending_req);
            if (it->second.pending_timer) { boost::system::error_code ec; it->second.pending_timer->cancel(ec); }
            it->second.pending_timer.reset();
        }
        if (socket && req) {
            http::response<http::string_body> res{http::status::ok, req->version()}; fill_cors_headers(res, *req);
            res.set(http::field::cache_control, "no-store, no-cache, must-revalidate, proxy-revalidate"); res.set(http::field::content_type, "text/plain; charset=UTF-8"); res.body() = std::move(body); res.prepare_payload(); write_and_close(socket, std::move(res));
            LOG_TRACE("Unified long-poll timeout responded for conn ", conn_id);
        }
    }

private:
    asio::io_service& m_io_service;
    tcp::acceptor m_acceptor;
    tcp::socket m_socket;

    mutable std::mutex m_handler_mutex;
    std::shared_ptr<TransportEventHandler> m_event_handler;

    mutable std::mutex m_mutex;
    std::unordered_map<ConnectionId, std::shared_ptr<ConnectionInfo>> m_conn_info;
    std::unordered_map<ConnectionId, PollState> m_polling;
    std::unordered_map<std::string, ConnectionId> m_sid_to_conn;
    std::unordered_map<ConnectionId, std::shared_ptr<WSSession>> m_ws_sessions;
};

class UnifiedTransportFactory : public TransportFactory {
public:
    std::unique_ptr<Transport> create_transport(asio::io_service& io_service) override {
        return std::make_unique<UnifiedTransport>(io_service);
    }
    std::string get_transport_type() const override { return "unified"; }
};

} // namespace transport
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_UNIFIED_TRANSPORT_HPP
