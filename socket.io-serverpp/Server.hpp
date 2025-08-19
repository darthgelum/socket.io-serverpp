#ifndef SOCKETIO_SERVERPP_SERVER_HPP
#define SOCKETIO_SERVERPP_SERVER_HPP

#include "config.hpp"
#include "Constants.hpp"
#include "Error.hpp"
#include "Logger.hpp"
#include "socketio/SocketIOServer.hpp"
#include "socketio/SocketNamespace.hpp"
#include "socketio/Socket.hpp"
#include "engineio/EngineIOServer.hpp"
#include "transport/Transport.hpp"
#include "transport/WebSocketTransport.hpp"
#include <memory>
#include <string>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

/**
 * @brief Main Socket.IO server class with simplified interface
 * 
 * This class provides a simplified interface to the layered architecture,
 * making it easy to migrate from the old API while providing access to
 * the new transport and Engine.IO layers.
 */
class Server {
public:
    /**
     * @brief Constructs a new Socket.IO server
     * @param io_service IO service for async operations
     */
    explicit Server(boost::asio::io_service& io_service)
        : m_io_service(io_service) {
        
        // Create Engine.IO configuration
        engineio::EngineIOConfig config;
        config.ping_interval = timeouts::DEFAULT_PING_INTERVAL;
        config.ping_timeout = timeouts::DEFAULT_PING_TIMEOUT;
        config.max_payload = timeouts::DEFAULT_MAX_PAYLOAD;
        config.allow_upgrades = false;  // WebSocket only for now
        
        // Create Socket.IO server using factory method
        m_socket_io_server = socketio::SocketIOServer::create(io_service, config);
        
        LOG_INFO("Socket.IO server initialized with layered architecture");
    }
    
    /**
     * @brief Starts listening on specified WebSocket port
     * @param websocket_port WebSocket server port
     */
    void listen(int websocket_port) {
        if (!m_socket_io_server) {
            throw SocketIOException("Server not initialized", SocketIOErrorCode::INVALID_STATE);
        }
        
        m_socket_io_server->listen(websocket_port);
        LOG_INFO("Server listening on WebSocket port: ", websocket_port);
    }
    
    /**
     * @brief Backward compatibility: listen with address and ports
     * @param http_address HTTP server address (ignored, WebSocket only)
     * @param http_port HTTP server port (ignored, WebSocket only)
     * @param websocket_port WebSocket server port
     */
    void listen(const std::string& http_address, int http_port, int websocket_port) {
        // Just use the WebSocket port, ignore HTTP parameters
        listen(websocket_port);
        LOG_INFO("Server listening - WebSocket only on port: ", websocket_port, 
                 " (HTTP handshake deprecated)");
    }
    
    
    /**
     * @brief Creates or retrieves a namespace
     * @param nsp Namespace name
     * @return Shared pointer to SocketNamespace
     */
    std::shared_ptr<SocketNamespace> of(const std::string& nsp) {
        if (!m_socket_io_server) {
            throw SocketIOException("Server not initialized", SocketIOErrorCode::INVALID_STATE);
        }
        
        return m_socket_io_server->of(nsp);
    }
    
    /**
     * @brief Gets the default namespace
     * @return Default namespace
     */
    std::shared_ptr<SocketNamespace> sockets() {
        if (!m_socket_io_server) {
            throw SocketIOException("Server not initialized", SocketIOErrorCode::INVALID_STATE);
        }
        
        return m_socket_io_server->sockets();
    }
    
    /**
     * @brief Runs the server event loop
     */
    void run() {
        if (!m_socket_io_server) {
            throw SocketIOException("Server not initialized", SocketIOErrorCode::INVALID_STATE);
        }
        
        LOG_INFO("Starting server event loop");
        m_socket_io_server->run();
    }
    
    /**
     * @brief Stops the server
     */
    void stop() {
        if (m_socket_io_server) {
            m_socket_io_server->stop();
            LOG_INFO("Server stopped");
        }
    }
    
    /**
     * @brief Gets access to the underlying Socket.IO server for advanced usage
     * @return Socket.IO server instance
     */
    std::shared_ptr<socketio::SocketIOServer> get_socket_io_server() {
        return m_socket_io_server;
    }
    
    /**
     * @brief Configures Engine.IO settings
     * @param ping_interval Ping interval in milliseconds
     * @param ping_timeout Ping timeout in milliseconds
     */
    void set_engine_io_config(int ping_interval, int ping_timeout) {
        // Note: This would require recreating the server with new config
        // For now, log a warning about runtime configuration
        LOG_WARN("Engine.IO configuration should be set before server creation. "
                 "Runtime configuration changes not supported yet.");
    }

private:
    boost::asio::io_service& m_io_service;
    std::shared_ptr<socketio::SocketIOServer> m_socket_io_server;
};

} // namespace lib

// Export types to main namespace for backward compatibility
using lib::Server;
using lib::SocketNamespace;
using lib::Socket;

// Also export new layered components for advanced usage
namespace transport {
    using namespace lib::transport;
}

namespace engineio {
    using namespace lib::engineio;
}

namespace socketio {
    using namespace lib::socketio;
}

} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_SERVER_HPP
