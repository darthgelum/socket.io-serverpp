#ifndef SOCKETIO_SERVERPP_HPP
#define SOCKETIO_SERVERPP_HPP

/**
 * @file socketio-serverpp.hpp
 * @brief Main header for Socket.IO Server++ with layered architecture
 * 
 * This header provides access to the complete Socket.IO server implementation
 * with separated transport, Engine.IO, and Socket.IO layers.
 * 
 * Architecture Overview:
 * =====================
 * 
 * Transport Layer:
 * - Abstract transport interface for WebSocket, HTTP polling, etc.
 * - WebSocketTransport: WebSocket implementation using websocketpp
 * - Future: HTTPPollingTransport for long polling support
 * 
 * Engine.IO Layer:
 * - EngineIOSession: Manages individual Engine.IO sessions with heartbeat
 * - EngineIOServer: Manages multiple sessions and transport coordination
 * - Handles Engine.IO protocol (ping/pong, upgrades, etc.)
 * 
 * Socket.IO Layer:
 * - SocketIOServer: Main Socket.IO server handling namespaces and routing
 * - SocketNamespace: Manages sockets within a namespace
 * - Socket: Individual socket connection with event handling
 * 
 * Simplified Interface:
 * - Server: Backward-compatible main server class
 * 
 * Usage Example:
 * ==============
 * 
 * Basic usage (backward compatible):
 * ```cpp
 * asio::io_service io;
 * socketio_serverpp::Server server(io);
 * 
 * server.sockets()->onConnection([](socketio_serverpp::Socket& socket) {
 *     socket.on("message", [&](const socketio_serverpp::Event& event) {
 *         socket.emit("echo", event.args[0]);
 *     });
 * });
 * 
 * server.listen("0.0.0.0", 3001, 3000);  // HTTP port 3001, WebSocket port 3000
 * server.run();
 * ```
 * 
 * Advanced usage with direct layer access:
 * ```cpp
 * asio::io_service io;
 * 
 * // Create Engine.IO server with custom config
 * socketio_serverpp::engineio::EngineIOConfig config;
 * config.ping_interval = 30000;
 * auto eio_server = std::make_shared<socketio_serverpp::engineio::EngineIOServer>(io, config);
 * 
 * // Add multiple transports
 * auto ws_transport = std::make_unique<socketio_serverpp::transport::WebSocketTransport>(io);
 * eio_server->add_transport(std::move(ws_transport));
 * 
 * // Create Socket.IO server on top
 * auto sio_server = socketio_serverpp::socketio::SocketIOServer::create(io, config);
 * ```
 */

// Core configuration and common headers
#include "config.hpp"
#include "Constants.hpp"
#include "Error.hpp"
#include "Logger.hpp"
#include "Message.hpp"
#include "Event.hpp"
#include "uuid.hpp"

// Transport layer
#include "transport/Transport.hpp"
#include "transport/WebSocketTransport.hpp"

// Engine.IO layer
#include "engineio/EngineIOSession.hpp"
#include "engineio/EngineIOServer.hpp"

// Socket.IO layer
#include "socketio/SocketIOServer.hpp"
#include "socketio/SocketNamespace.hpp"
#include "socketio/Socket.hpp"

// Main server interface (backward compatible)
#include "Server.hpp"

/**
 * @brief Main namespace for Socket.IO Server++
 */
namespace SOCKETIO_SERVERPP_NAMESPACE {

// Version information
namespace version {
    constexpr const char* VERSION = "2.0.0";
    constexpr const char* DESCRIPTION = "Socket.IO Server++ with layered architecture";
    constexpr int VERSION_MAJOR = 2;
    constexpr int VERSION_MINOR = 0;
    constexpr int VERSION_PATCH = 0;
}

// Convenience type aliases for common usage
using SocketIOServer = lib::socketio::SocketIOServer;
using EngineIOServer = lib::engineio::EngineIOServer;
using WebSocketTransport = lib::transport::WebSocketTransport;

// Backward compatibility aliases
using Server = lib::Server;
using SocketNamespace = lib::SocketNamespace;
using Socket = lib::Socket;
using Event = lib::Event;
using Message = lib::Message;

// Configuration types
using EngineIOConfig = lib::engineio::EngineIOConfig;

// Transport types
namespace Transport {
    using Interface = lib::transport::Transport;
    using WebSocket = lib::transport::WebSocketTransport;
    using Factory = lib::transport::TransportFactory;
    using ConnectionHandle = lib::transport::ConnectionHandle;
    using ConnectionInfo = lib::transport::ConnectionInfo;
    using Message = lib::transport::TransportMessage;
}

// Error types
using SocketIOException = lib::SocketIOException;
using SocketIOErrorCode = lib::SocketIOErrorCode;

/**
 * @brief Creates a new Socket.IO server with default configuration
 * @param io_service Boost ASIO IO service
 * @return Shared pointer to Server instance
 */
inline std::shared_ptr<Server> create_server(boost::asio::io_service& io_service) {
    return std::make_shared<Server>(io_service);
}

/**
 * @brief Creates a new Socket.IO server with custom Engine.IO configuration
 * @param io_service Boost ASIO IO service
 * @param config Engine.IO configuration
 * @return Shared pointer to SocketIOServer instance
 */
inline std::shared_ptr<SocketIOServer> create_socket_io_server(
    boost::asio::io_service& io_service, 
    const EngineIOConfig& config = EngineIOConfig()) {
    return SocketIOServer::create(io_service, config);
}

/**
 * @brief Creates a new WebSocket transport
 * @param io_service Boost ASIO IO service
 * @return Unique pointer to WebSocketTransport instance
 */
inline std::unique_ptr<WebSocketTransport> create_websocket_transport(boost::asio::io_service& io_service) {
    return std::make_unique<WebSocketTransport>(io_service);
}

} // namespace SOCKETIO_SERVERPP_NAMESPACE

// Global convenience alias for the main namespace
namespace socketio_serverpp = SOCKETIO_SERVERPP_NAMESPACE;

#endif // SOCKETIO_SERVERPP_HPP
