#ifndef SOCKETIO_SERVERPP_WEBSOCKET_TRANSPORT_HPP
#define SOCKETIO_SERVERPP_WEBSOCKET_TRANSPORT_HPP

#include "Transport.hpp"
#include "../config.hpp"
#include "../Logger.hpp"
#include "../uuid.hpp"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace transport {

/**
 * @brief WebSocket transport implementation
 */
class WebSocketTransport : public Transport {
public:
    explicit WebSocketTransport(asio::io_service& io_service)
        : m_io_service(io_service) {
        initialize_server();
    }
    
    ~WebSocketTransport() override {
        stop();
    }
    
    void set_event_handler(std::shared_ptr<TransportEventHandler> handler) override {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        m_event_handler = handler;
    }
    
    void listen(const std::string& address, int port) override {
        try {
            m_server.listen(port);
            LOG_INFO("WebSocket transport listening on port: ", port);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start WebSocket transport: ", e.what());
            throw SocketIOException("Failed to start WebSocket transport", 
                                  SocketIOErrorCode::CONNECTION_FAILED);
        }
    }
    
    void start_accept() override {
        m_server.start_accept();
        LOG_DEBUG("WebSocket transport started accepting connections");
    }
    
    bool send_message(const ConnectionHandle& connection, 
                     const std::string& message,
                     bool is_binary = false) override {
        try {
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            auto it = m_id_to_handle.find(connection.id);
            if (it == m_id_to_handle.end()) {
                LOG_WARN("Invalid connection handle for send");
                return false;
            }
            
            auto opcode = is_binary ? wspp::frame::opcode::binary : wspp::frame::opcode::text;
            m_server.send(it->second, message, opcode);
            LOG_TRACE("Sent message to connection: ", connection.id);
            return true;
        } catch (const std::exception& e) {
            LOG_WARN("Failed to send message to connection ", connection.id, ": ", e.what());
            return false;
        }
    }
    
    void close_connection(const ConnectionHandle& connection,
                         int code = 1000,
                         const std::string& reason = "Normal closure") override {
        try {
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            auto it = m_id_to_handle.find(connection.id);
            if (it == m_id_to_handle.end()) {
                LOG_WARN("Invalid connection handle for close");
                return;
            }
            
            websocketpp::close::status::value close_code = 
                static_cast<websocketpp::close::status::value>(code);
            m_server.close(it->second, close_code, reason);
            LOG_DEBUG("Closed connection: ", connection.id);
        } catch (const std::exception& e) {
            LOG_WARN("Failed to close connection ", connection.id, ": ", e.what());
        }
    }
    
    void stop() override {
        try {
            m_server.stop();
            std::lock_guard<std::mutex> lock(m_connections_mutex);
            m_connections.clear();
            m_handle_to_id.clear();
            m_id_to_handle.clear();
            LOG_INFO("WebSocket transport stopped");
        } catch (const std::exception& e) {
            LOG_WARN("Error stopping WebSocket transport: ", e.what());
        }
    }
    
    std::string get_name() const override {
        return "websocket";
    }
    
    bool supports_binary() const override {
        return true;
    }
    
    std::shared_ptr<ConnectionInfo> get_connection_info(
        const ConnectionHandle& connection) const override {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        auto it = m_connections.find(connection.id);
        if (it != m_connections.end()) {
            return it->second;
        }
        return nullptr;
    }

private:
    void initialize_server() {
        m_server.init_asio(&m_io_service);
        m_server.set_access_channels(websocketpp::log::alevel::none);
        m_server.set_error_channels(websocketpp::log::elevel::warn);
        
        // Set handlers
        m_server.set_open_handler([this](wspp::connection_hdl hdl) {
            on_websocket_open(hdl);
        });
        
        m_server.set_message_handler([this](wspp::connection_hdl hdl, wsserver::message_ptr msg) {
            on_websocket_message(hdl, msg);
        });
        
        m_server.set_close_handler([this](wspp::connection_hdl hdl) {
            on_websocket_close(hdl);
        });
        
        LOG_DEBUG("WebSocket transport server initialized");
    }
    
    void on_websocket_open(wspp::connection_hdl hdl) {
        try {
            auto connection = m_server.get_con_from_hdl(hdl);
            std::string conn_id = generate_connection_id();
            
            // Store connection mapping
            {
                std::lock_guard<std::mutex> lock(m_connections_mutex);
                auto conn_info = std::make_shared<ConnectionInfo>(conn_id);
                
                // Extract connection details
                conn_info->remote_address = connection->get_remote_endpoint();
                conn_info->user_agent = connection->get_request_header("User-Agent");
                
                // Parse query parameters from resource
                std::string resource = connection->get_resource();
                parse_query_parameters(resource, conn_info->query_params);
                
                // Note: websocketpp doesn't provide get_request_headers(), 
                // so we'll just store the user agent for now
                // Individual headers can be accessed with get_request_header("Header-Name")
                
                m_connections[conn_id] = conn_info;
                m_handle_to_id[hdl] = conn_id;
                m_id_to_handle[conn_id] = hdl;
            }
            
            // Notify handler
            auto handler = get_event_handler();
            if (handler) {
                ConnectionInfo info(conn_id);
                info.remote_address = connection->get_remote_endpoint();
                info.user_agent = connection->get_request_header("User-Agent");
                
                std::string resource = connection->get_resource();
                parse_query_parameters(resource, info.query_params);
                
                handler->on_connection_open(info);
            }
            
            LOG_DEBUG("WebSocket connection opened: ", conn_id);
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling WebSocket open: ", e.what());
        }
    }
    
    void on_websocket_message(wspp::connection_hdl hdl, wsserver::message_ptr msg) {
        try {
            std::string conn_id = get_connection_id(hdl);
            if (conn_id.empty()) {
                LOG_WARN("Received message from unknown connection");
                return;
            }
            
            ConnectionHandle conn_handle(conn_id);
            TransportMessage transport_msg(conn_handle, msg->get_payload(), 
                                         msg->get_opcode() == wspp::frame::opcode::binary);
            
            auto handler = get_event_handler();
            if (handler) {
                handler->on_message(transport_msg);
            }
            
            LOG_TRACE("Received message from connection: ", conn_id);
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling WebSocket message: ", e.what());
        }
    }
    
    void on_websocket_close(wspp::connection_hdl hdl) {
        try {
            std::string conn_id = get_connection_id(hdl);
            if (conn_id.empty()) {
                LOG_WARN("Connection close for unknown connection");
                return;
            }
            
            // Cleanup connection mapping
            {
                std::lock_guard<std::mutex> lock(m_connections_mutex);
                m_connections.erase(conn_id);
                m_handle_to_id.erase(hdl);
                m_id_to_handle.erase(conn_id);
            }
            
            ConnectionHandle conn_handle(conn_id);
            auto handler = get_event_handler();
            if (handler) {
                handler->on_connection_close(conn_handle, 1000, "WebSocket closed");
            }
            
            LOG_DEBUG("WebSocket connection closed: ", conn_id);
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling WebSocket close: ", e.what());
        }
    }
    
    std::string generate_connection_id() {
        return lib::uuid::uuid1();
    }
    
    std::string get_connection_id(wspp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        auto it = m_handle_to_id.find(hdl);
        return (it != m_handle_to_id.end()) ? it->second : std::string();
    }
    
    std::shared_ptr<TransportEventHandler> get_event_handler() {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        return m_event_handler;
    }
    
    void parse_query_parameters(const std::string& resource, 
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
            
            if (!key.empty()) {
                params[key] = value;
            }
            
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
    }

private:
    asio::io_service& m_io_service;
    wsserver m_server;
    
    mutable std::mutex m_handler_mutex;
    std::shared_ptr<TransportEventHandler> m_event_handler;
    
    mutable std::mutex m_connections_mutex;
    std::unordered_map<ConnectionId, std::shared_ptr<ConnectionInfo>> m_connections;
    std::map<wspp::connection_hdl, ConnectionId, std::owner_less<wspp::connection_hdl>> m_handle_to_id;
    std::unordered_map<ConnectionId, wspp::connection_hdl> m_id_to_handle;
};

/**
 * @brief WebSocket transport factory
 */
class WebSocketTransportFactory : public TransportFactory {
public:
    std::unique_ptr<Transport> create_transport(asio::io_service& io_service) override {
        return std::make_unique<WebSocketTransport>(io_service);
    }
    
    std::string get_transport_type() const override {
        return "websocket";
    }
};

} // namespace transport
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_WEBSOCKET_TRANSPORT_HPP
