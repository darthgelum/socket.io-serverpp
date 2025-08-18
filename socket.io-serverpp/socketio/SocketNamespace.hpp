#ifndef SOCKETIO_SERVERPP_SOCKET_NAMESPACE_V2_HPP
#define SOCKETIO_SERVERPP_SOCKET_NAMESPACE_V2_HPP

#include "../Constants.hpp"
#include "../Error.hpp"
#include "../Logger.hpp"
#include "../Message.hpp"
#include "../config.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../lib/rapidjson/document.h"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

// Forward declarations
namespace socketio {
    class SocketIOServer;
}
class Socket;

/**
 * @brief Updated SocketNamespace that works with layered architecture
 */
class SocketNamespace {
    friend class socketio::SocketIOServer;

public:
    /**
     * @brief Constructs a new SocketNamespace
     * @param nsp Namespace name
     * @param server Reference to the Socket.IO server
     */
    SocketNamespace(const std::string& nsp, socketio::SocketIOServer& server)
        : m_namespace(nsp), m_server(server) {
        LOG_INFO("SocketNamespace created: ", nsp.empty() ? "/" : nsp);
    }

    /**
     * @brief Sets connection event handler
     * @param cb Callback to execute when a socket connects
     */
    void onConnection(std::function<void(Socket&)> cb) {
        std::lock_guard<std::mutex> lock(m_handlers_mutex);
        m_connection_handlers.push_back(cb);
        LOG_DEBUG("Connection handler registered for namespace: ", m_namespace);
    }

    /**
     * @brief Sets disconnection event handler
     * @param cb Callback to execute when a socket disconnects
     */
    void onDisconnection(std::function<void(Socket&)> cb) {
        std::lock_guard<std::mutex> lock(m_handlers_mutex);
        m_disconnection_handlers.push_back(cb);
        LOG_DEBUG("Disconnection handler registered for namespace: ", m_namespace);
    }

    /**
     * @brief Sends data to all sockets in this namespace
     * @param data JSON data to send
     */
    void send(const std::string& data);

    /**
     * @brief Emits an event to all sockets in this namespace
     * @param name Event name
     * @param data Event data
     */
    void emit(const std::string& name, const std::string& data);

    /**
     * @brief Gets the namespace name
     * @return Namespace string
     */
    const std::string& socketNamespace() const {
        return m_namespace;
    }

    /**
     * @brief Gets the number of connected sockets
     * @return Socket count
     */
    size_t socket_count() const {
        std::lock_guard<std::mutex> lock(m_sockets_mutex);
        return m_sockets.size();
    }

    /**
     * @brief Checks if a specific session is connected to this namespace
     * @param session_id Engine.IO session ID
     * @return true if socket exists, false otherwise
     */
    bool has_session(const std::string& session_id) const {
        std::lock_guard<std::mutex> lock(m_sockets_mutex);
        return m_sockets.find(session_id) != m_sockets.end();
    }

    /**
     * @brief Broadcasts event to all sessions except sender
     * @param sender_session_id Session ID of sender (excluded from broadcast)
     * @param event_data Event data to broadcast
     */
    void broadcast_from_session(const std::string& sender_session_id, const std::string& event_data);

private:
    /**
     * @brief Called when a session connects to this namespace
     * @param session_id Engine.IO session ID
     */
    void on_session_connect(const std::string& session_id);

    /**
     * @brief Called when a session disconnects from this namespace
     * @param session_id Engine.IO session ID
     */
    void on_session_disconnect(const std::string& session_id);

    /**
     * @brief Called when an event is received from a session
     * @param session_id Engine.IO session ID
     * @param message Event message
     */
    void on_session_event(const std::string& session_id, const Message& message);

    /**
     * @brief Creates a socket instance for the session
     * @param session_id Engine.IO session ID
     * @return Socket instance
     */
    std::shared_ptr<Socket> create_socket(const std::string& session_id);

    /**
     * @brief Removes socket for session
     * @param session_id Engine.IO session ID
     */
    void remove_socket(const std::string& session_id);

    /**
     * @brief Calls connection handlers for a socket
     * @param socket Socket instance
     */
    void call_connection_handlers(Socket& socket);

    /**
     * @brief Calls disconnection handlers for a socket
     * @param socket Socket instance
     */
    void call_disconnection_handlers(Socket& socket);

private:
    const std::string m_namespace;
    socketio::SocketIOServer& m_server;

    mutable std::mutex m_sockets_mutex;
    std::unordered_map<std::string, std::shared_ptr<Socket>> m_sockets;  // session_id -> Socket

    mutable std::mutex m_handlers_mutex;
    std::vector<std::function<void(Socket&)>> m_connection_handlers;
    std::vector<std::function<void(Socket&)>> m_disconnection_handlers;
};

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_SOCKET_NAMESPACE_V2_HPP
