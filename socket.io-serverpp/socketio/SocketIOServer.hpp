#ifndef SOCKETIO_SERVERPP_SOCKET_IO_SERVER_HPP
#define SOCKETIO_SERVERPP_SOCKET_IO_SERVER_HPP

#include "../config.hpp"
#include "../Constants.hpp"
#include "../Error.hpp"
#include "../Logger.hpp"
#include "../Message.hpp"
#include "../transport/Transport.hpp"
#include "../transport/WebSocketTransport.hpp"
#include "../transport/PollingTransport.hpp"
#include "../transport/UnifiedTransport.hpp"
#include "../engineio/EngineIOServer.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <boost/beast/http.hpp>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

// Forward declarations
class SocketNamespace;

namespace socketio {

/**
 * @brief Socket.IO server implementing the Socket.IO protocol on top of Engine.IO
 */
class SocketIOServer : public engineio::EngineIOServerHandler,
                      public std::enable_shared_from_this<SocketIOServer> {
private:
    // Private tag for constructor access control
    struct PrivateTag {};
    
public:
    /**
     * @brief Creates a new Socket.IO server (factory method)
     * @param io_service IO service for async operations
     * @param config Engine.IO configuration
     * @return Shared pointer to initialized SocketIOServer
     */
    static std::shared_ptr<SocketIOServer> create(boost::asio::io_service& io_service, 
                                                 const engineio::EngineIOConfig& config = engineio::EngineIOConfig()) {
        // Create the server using the tagged constructor
        auto server = std::make_shared<SocketIOServer>(PrivateTag{}, io_service, config);
        
        // Now we can safely call initialization
        server->initialize();
        
        return server;
    }
    
    /**
     * @brief Tagged constructor (use create() method instead)
     * @param tag Private tag to prevent direct construction
     * @param io_service IO service for async operations
     * @param config Engine.IO configuration
     */
    explicit SocketIOServer(PrivateTag, boost::asio::io_service& io_service, 
                           const engineio::EngineIOConfig& config = engineio::EngineIOConfig())
        : m_io_service(io_service)
        , m_engine_io_server(std::make_shared<engineio::EngineIOServer>(io_service, config))
        , m_initialized(false) {
        
        LOG_INFO("Socket.IO server created (initialization pending)");
    }
    
    /**
     * @brief Initialize the server (called automatically by factory method)
     */
    void initialize() {
        if (m_initialized) return;
        
        // Set up Engine.IO event handling
        m_engine_io_server->set_handler(shared_from_this());
        
        // Create default namespace
        m_sockets = of("/");
        
        m_initialized = true;
        LOG_INFO("Socket.IO server initialized");
    }

    
    /**
     * @brief Starts listening on a single port supporting both Polling and WebSocket (auto-upgrade)
     * @param port Port for both HTTP and WebSocket
     */
    void listen(int port) {
        // Ensure initialization is complete before listening
        initialize();
        
        try {
            // Prefer unified transport if available; ensure shared_ptr is of the concrete type
            auto unified_unique = transport::UnifiedTransportFactory{}.create_transport(m_io_service);
            auto unified_raw = dynamic_cast<transport::UnifiedTransport*>(unified_unique.get());
            std::shared_ptr<transport::Transport> unified_transport;
            if (unified_raw) {
                std::shared_ptr<transport::UnifiedTransport> unified_shared(unified_raw);
                unified_unique.release();
                unified_transport = unified_shared;
            } else {
                // Fallback to base shared_ptr (shouldn't happen)
                unified_transport = std::shared_ptr<transport::Transport>(unified_unique.release());
            }
            m_engine_io_server->add_transport(unified_transport);
            
            // Start Engine.IO server
            m_engine_io_server->listen("0.0.0.0", port);
            m_engine_io_server->start_accept();
            
            LOG_INFO("Socket.IO server listening (unified) on port: ", port);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start Socket.IO server: ", e.what());
            throw SocketIOException("Failed to start Socket.IO server", 
                                  SocketIOErrorCode::CONNECTION_FAILED);
        }
    }

    /**
     * @brief Starts listening on WebSocket and Polling ports (separate ports for now)
     * @param ws_port WebSocket port
     * @param polling_port HTTP polling port
     */
    void listen(int ws_port, int polling_port) {
        // Ensure initialization is complete before listening
        initialize();

        try {
            // Add WebSocket transport
            auto ws_unique = transport::WebSocketTransportFactory{}.create_transport(m_io_service);
            auto ws_transport = std::shared_ptr<transport::Transport>(ws_unique.release());
            m_engine_io_server->add_transport(ws_transport);

            // Add Polling transport (construct shared_ptr with concrete type for shared_from_this)
            auto polling_unique = transport::PollingTransportFactory{}.create_transport(m_io_service);
            auto polling_raw = dynamic_cast<transport::PollingTransport*>(polling_unique.get());
            std::shared_ptr<transport::Transport> polling_transport;
            if (polling_raw) {
                // Create shared_ptr with the exact derived type so enable_shared_from_this works
                std::shared_ptr<transport::PollingTransport> polling_shared(polling_raw);
                polling_unique.release();
                polling_transport = polling_shared;
            } else {
                // Fallback (should not happen)
                polling_transport = std::shared_ptr<transport::Transport>(polling_unique.release());
            }
            m_engine_io_server->add_transport(polling_transport);

            // Listen transports on their respective ports
            ws_transport->listen("0.0.0.0", ws_port);
            polling_transport->listen("0.0.0.0", polling_port);

            // Start accepting on all transports
            m_engine_io_server->start_accept();

            LOG_INFO("Socket.IO server listening - WS:", ws_port, ", Polling:", polling_port);

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start Socket.IO server (ws+polling): ", e.what());
            throw SocketIOException("Failed to start Socket.IO server (ws+polling)", SocketIOErrorCode::CONNECTION_FAILED);
        }
    }
    
    /**
     * @brief Creates or retrieves a namespace
     * @param nsp Namespace name
     * @return Shared pointer to SocketNamespace
     */
    std::shared_ptr<SocketNamespace> of(const std::string& nsp);
    
    /**
     * @brief Creates or retrieves a namespace (alias for of)
     * @param nsp Namespace name
     * @return Shared pointer to SocketNamespace
     */
    std::shared_ptr<SocketNamespace> get_namespace(const std::string& nsp) {
        // Ensure initialization is complete before creating namespaces
        initialize();
        return of(nsp);
    }
    
    /**
     * @brief Gets the default namespace
     * @return Default namespace
     */
    std::shared_ptr<SocketNamespace> sockets() const {
        return m_sockets;
    }
    
    /**
     * @brief Runs the server event loop
     */
    void run() {
        LOG_INFO("Starting Socket.IO server event loop");
        m_io_service.run();
    }
    
    /**
     * @brief Stops the server
     */
    void stop() {
        LOG_INFO("Stopping Socket.IO server");
        
        // Clear namespaces
        {
            std::lock_guard<std::mutex> lock(m_namespaces_mutex);
            m_namespaces.clear();
        }
        
        // Stop Engine.IO server
        if (m_engine_io_server) {
            m_engine_io_server->stop();
        }
        
        LOG_INFO("Socket.IO server stopped");
    }
    
    /**
     * @brief Sends a Socket.IO message to a session
     * @param session_id Engine.IO session ID
     * @param packet_type Socket.IO packet type
     * @param nsp Namespace
     * @param data Message data
     * @return true if sent successfully
     */
    bool send_socket_io_message(const std::string& session_id, 
                               int packet_type,
                               const std::string& nsp,
                               const std::string& data) {
        if (!m_engine_io_server) {
            LOG_WARN("Engine.IO server not available");
            return false;
        }
        
        // Build Socket.IO packet
        std::string packet = std::to_string(packet_type);
        
        // Add namespace if not default
        if (!nsp.empty() && nsp != "/") {
            packet += nsp + ",";
        }
        
        // Add data
        packet += data;
        
        bool success = m_engine_io_server->send_to_session(session_id, packet);
        if (success) {
            LOG_TRACE("Sent Socket.IO message to session: ", session_id);
        } else {
            LOG_WARN("Failed to send Socket.IO message to session: ", session_id);
        }
        
        return success;
    }
    
    /**
     * @brief Closes a Socket.IO session
     * @param session_id Session to close
     * @param reason Close reason
     */
    void close_session(const std::string& session_id, const std::string& reason = "Server close") {
        if (m_engine_io_server) {
            m_engine_io_server->close_session(session_id, reason);
        }
    }

    // EngineIOServerHandler interface
    void on_engine_io_session_open(const std::string& session_id) override;
    
    void on_engine_io_message(const std::string& session_id, const std::string& message) override;
    
    void on_engine_io_session_close(const std::string& session_id, const std::string& reason) override;
    
    void on_engine_io_session_error(const std::string& session_id, const std::string& error) override;

private:
    /**
     * @brief Session state for Socket.IO
     */
    struct SessionState {
        std::string current_namespace = "";  // Current namespace for this session
        bool connected_to_namespace = false;
    };
    
    void handle_socket_io_message(const std::string& session_id, const std::string& sio_payload) {
        if (sio_payload.empty()) {
            LOG_WARN("Empty Socket.IO payload from session: ", session_id);
            return;
        }
        
        // Parse Socket.IO packet
        int packet_type = sio_payload[0] - '0';
        if (packet_type < 0 || packet_type > 6) {
            LOG_WARN("Invalid Socket.IO packet type: ", packet_type, " from session: ", session_id);
            return;
        }
        
        size_t idx = 1;
        std::string nsp;
        
        // Parse namespace
        if (idx < sio_payload.size() && sio_payload[idx] == '/') {
            size_t comma = sio_payload.find(',', idx);
            if (comma != std::string::npos) {
                nsp = sio_payload.substr(idx, comma - idx);
                idx = comma + 1;
            } else {
                nsp = sio_payload.substr(idx);
                idx = sio_payload.size();
            }
        }
        
        // Skip ack ID
        while (idx < sio_payload.size() && 
               std::isdigit(static_cast<unsigned char>(sio_payload[idx]))) {
            ++idx;
        }
        
        std::string data = (idx < sio_payload.size()) ? sio_payload.substr(idx) : std::string();
        
        LOG_TRACE("Parsed Socket.IO packet - type: ", packet_type, ", namespace: ", 
                  nsp.empty() ? "/" : nsp, ", session: ", session_id);
        
        // Handle packet
        handle_socket_io_packet(session_id, packet_type, nsp, data);
    }
    
    void handle_socket_io_packet(const std::string& session_id, int packet_type, 
                                const std::string& nsp, const std::string& data) {
        switch (packet_type) {
        case socket_io::CONNECT:
            // Accept CONNECT and send ack within handle_socket_io_connect
            handle_socket_io_connect(session_id, nsp);
            break;
            
        case socket_io::DISCONNECT:
            handle_socket_io_disconnect(session_id, nsp);
            break;
            
        case socket_io::EVENT:
            handle_socket_io_event(session_id, nsp, data);
            break;
            
        case socket_io::ACK:
            // TODO: Implement acknowledgments
            LOG_DEBUG("Socket.IO ACK received (not implemented)");
            break;
            
        case socket_io::CONNECT_ERROR:
            LOG_WARN("Received CONNECT_ERROR from client");
            break;
            
        case socket_io::BINARY_EVENT:
        case socket_io::BINARY_ACK:
            // TODO: Implement binary support
            LOG_DEBUG("Binary Socket.IO packet received (not implemented)");
            break;
            
        default:
            LOG_WARN("Unknown Socket.IO packet type: ", packet_type);
            break;
        }
    }
    
    void handle_socket_io_connect(const std::string& session_id, const std::string& nsp);
    
    void handle_socket_io_disconnect(const std::string& session_id, const std::string& nsp);
    
    void handle_socket_io_event(const std::string& session_id, const std::string& nsp, 
                               const std::string& data);
    
    std::shared_ptr<SocketNamespace> find_or_create_namespace(const std::string& nsp);

private:
    boost::asio::io_service& m_io_service;
    std::shared_ptr<engineio::EngineIOServer> m_engine_io_server;
    
    std::shared_ptr<SocketNamespace> m_sockets;  // Default namespace
    
    mutable std::mutex m_namespaces_mutex;
    std::unordered_map<std::string, std::shared_ptr<SocketNamespace>> m_namespaces;
    
    mutable std::mutex m_sessions_mutex;
    std::unordered_map<std::string, SessionState> m_sessions;
    
    bool m_initialized;  // Track initialization state
};

} // namespace socketio
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_SOCKET_IO_SERVER_HPP
