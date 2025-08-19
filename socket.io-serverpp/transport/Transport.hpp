#ifndef SOCKETIO_SERVERPP_TRANSPORT_HPP
#define SOCKETIO_SERVERPP_TRANSPORT_HPP

#include "../config.hpp"
#include "../Constants.hpp"
#include "../Error.hpp"
#include <functional>
#include <memory>
#include <string>
#include <map>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace transport {

/**
 * @brief Transport connection identifier
 */
using ConnectionId = std::string;

/**
 * @brief Transport connection handle (can be extended for different transport types)
 */
struct ConnectionHandle {
    ConnectionId id;
    void* native_handle;  // Transport-specific handle (e.g., underlying socket/stream)
    
    ConnectionHandle(const ConnectionId& conn_id, void* handle = nullptr) 
        : id(conn_id), native_handle(handle) {}
    
    bool operator<(const ConnectionHandle& other) const {
        return id < other.id;
    }
    
    bool operator==(const ConnectionHandle& other) const {
        return id == other.id;
    }
    
    bool operator!=(const ConnectionHandle& other) const {
        return id != other.id;
    }
};

/**
 * @brief Transport message representation
 */
struct TransportMessage {
    ConnectionHandle connection;
    std::string payload;
    bool is_binary;
    
    TransportMessage(const ConnectionHandle& conn, const std::string& data, bool binary = false)
        : connection(conn), payload(data), is_binary(binary) {}
};

/**
 * @brief Transport connection information
 */
struct ConnectionInfo {
    ConnectionId id;
    std::string remote_address;
    std::string user_agent;
    std::map<std::string, std::string> query_params;
    std::map<std::string, std::string> headers;
    
    ConnectionInfo(const ConnectionId& conn_id) : id(conn_id) {}
};

/**
 * @brief Transport event handlers
 */
class TransportEventHandler {
public:
    virtual ~TransportEventHandler() = default;
    
    /**
     * @brief Called when a new connection is established
     * @param info Connection information
     */
    virtual void on_connection_open(const ConnectionInfo& info) = 0;
    
    /**
     * @brief Called when a message is received
     * @param message Transport message
     */
    virtual void on_message(const TransportMessage& message) = 0;
    
    /**
     * @brief Called when a connection is closed
     * @param connection Connection handle
     * @param code Close code
     * @param reason Close reason
     */
    virtual void on_connection_close(const ConnectionHandle& connection, 
                                   int code, const std::string& reason) = 0;
    
    /**
     * @brief Called when a transport error occurs
     * @param connection Connection handle  
     * @param error Error information
     */
    virtual void on_error(const ConnectionHandle& connection, 
                         const std::string& error) = 0;
};

/**
 * @brief Abstract transport interface
 */
class Transport {
public:
    virtual ~Transport() = default;
    
    /**
     * @brief Sets the event handler for this transport
     * @param handler Event handler
     */
    virtual void set_event_handler(std::shared_ptr<TransportEventHandler> handler) = 0;
    
    /**
     * @brief Starts listening for connections
     * @param address Address to bind to
     * @param port Port to listen on
     */
    virtual void listen(const std::string& address, int port) = 0;
    
    /**
     * @brief Starts accepting connections
     */
    virtual void start_accept() = 0;
    
    /**
     * @brief Sends a message to a specific connection
     * @param connection Target connection
     * @param message Message to send
     * @param is_binary Whether the message is binary
     * @return true if message was queued successfully
     */
    virtual bool send_message(const ConnectionHandle& connection, 
                             const std::string& message,
                             bool is_binary = false) = 0;
    
    /**
     * @brief Closes a connection
     * @param connection Connection to close
     * @param code Close code
     * @param reason Close reason
     */
    virtual void close_connection(const ConnectionHandle& connection,
                                 int code = 1000,
                                 const std::string& reason = "Normal closure") = 0;
    
    /**
     * @brief Stops the transport
     */
    virtual void stop() = 0;
    
    /**
     * @brief Gets the transport name
     * @return Transport name
     */
    virtual std::string get_name() const = 0;
    
    /**
     * @brief Checks if the transport supports binary messages
     * @return true if binary is supported
     */
    virtual bool supports_binary() const = 0;
    
    /**
     * @brief Gets connection information
     * @param connection Connection handle
     * @return Connection info or nullptr if not found
     */
    virtual std::shared_ptr<ConnectionInfo> get_connection_info(
        const ConnectionHandle& connection) const = 0;
};

/**
 * @brief Transport factory interface
 */
class TransportFactory {
public:
    virtual ~TransportFactory() = default;
    
    /**
     * @brief Creates a transport instance
     * @param io_service IO service for async operations
     * @return Transport instance
     */
    virtual std::unique_ptr<Transport> create_transport(asio::io_service& io_service) = 0;
    
    /**
     * @brief Gets the transport type name
     * @return Transport type name
     */
    virtual std::string get_transport_type() const = 0;
};

} // namespace transport
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_TRANSPORT_HPP
