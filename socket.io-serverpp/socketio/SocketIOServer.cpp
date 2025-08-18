#include "SocketIOServer.hpp"
#include "SocketNamespace.hpp"
#include "../uuid.hpp"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace socketio {

std::shared_ptr<SocketNamespace> SocketIOServer::of(const std::string& nsp) {
    std::lock_guard<std::mutex> lock(m_namespaces_mutex);
    auto iter = m_namespaces.find(nsp);
    if (iter == m_namespaces.end()) {
        auto socket_namespace = std::make_shared<SocketNamespace>(nsp, *this);
        m_namespaces.emplace(nsp, socket_namespace);
        LOG_DEBUG("Created Socket.IO namespace: ", nsp.empty() ? "/" : nsp);
        return socket_namespace;
    }
    return iter->second;
}

std::shared_ptr<SocketNamespace> SocketIOServer::find_or_create_namespace(const std::string& nsp) {
    return of(nsp);  // Same functionality
}

void SocketIOServer::on_engine_io_session_open(const std::string& session_id) {
    // Store session for Socket.IO use
    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    m_sessions.emplace(session_id, SessionState{});
    LOG_DEBUG("Socket.IO session opened: ", session_id);
}

void SocketIOServer::on_engine_io_message(const std::string& session_id, const std::string& message) {
    handle_socket_io_message(session_id, message);
}

void SocketIOServer::on_engine_io_session_close(const std::string& session_id, const std::string& reason) {
    // Notify all namespaces about disconnection
    {
        std::lock_guard<std::mutex> ns_lock(m_namespaces_mutex);
        for (const auto& ns_pair : m_namespaces) {
            ns_pair.second->on_session_disconnect(session_id);
        }
    }
    
    // Remove session
    {
        std::lock_guard<std::mutex> lock(m_sessions_mutex);
        m_sessions.erase(session_id);
    }
    
    LOG_DEBUG("Socket.IO session closed: ", session_id, " - ", reason);
}

void SocketIOServer::on_engine_io_session_error(const std::string& session_id, const std::string& error) {
    LOG_WARN("Socket.IO session error: ", session_id, " - ", error);
}

void SocketIOServer::handle_socket_io_connect(const std::string& session_id, const std::string& nsp) {
    // Find or use default namespace
    std::shared_ptr<SocketNamespace> socket_namespace;
    std::string target_nsp = nsp.empty() ? "/" : nsp;  // Default to "/" if empty
    
    {
        std::lock_guard<std::mutex> lock(m_namespaces_mutex);
        auto ns_it = m_namespaces.find(target_nsp);
        if (ns_it != m_namespaces.end()) {
            socket_namespace = ns_it->second;
        }
    }
    
    if (socket_namespace) {
        // Update session state
        {
            std::lock_guard<std::mutex> lock(m_sessions_mutex);
            auto session_it = m_sessions.find(session_id);
            if (session_it != m_sessions.end()) {
                session_it->second.current_namespace = target_nsp;
                session_it->second.connected_to_namespace = true;
            }
        }
        
        // Notify namespace
        socket_namespace->on_session_connect(session_id);
        
        // Send CONNECT response (use empty namespace in response for default namespace)
        std::string response_nsp = (target_nsp == "/") ? "" : target_nsp;
        send_socket_io_message(session_id, socket_io::CONNECT, response_nsp, 
                             "{\"sid\":\"" + session_id + "\"}");
        
        LOG_DEBUG("Socket.IO CONNECT to namespace: ", target_nsp, 
                  " for session: ", session_id);
    } else {
        // Send CONNECT_ERROR (use empty namespace in response for default namespace)
        std::string response_nsp = (target_nsp == "/") ? "" : target_nsp;
        send_socket_io_message(session_id, socket_io::CONNECT_ERROR, response_nsp,
                             "{\"message\":\"Namespace not found\"}");
        LOG_WARN("Socket.IO CONNECT to unknown namespace: ", target_nsp, " for session: ", session_id);
    }
}

void SocketIOServer::handle_socket_io_disconnect(const std::string& session_id, const std::string& nsp) {
    std::shared_ptr<SocketNamespace> socket_namespace;
    {
        std::lock_guard<std::mutex> lock(m_namespaces_mutex);
        auto ns_it = m_namespaces.find(nsp);
        if (ns_it != m_namespaces.end()) {
            socket_namespace = ns_it->second;
        }
    }
    
    if (socket_namespace) {
        socket_namespace->on_session_disconnect(session_id);
        
        // Update session state
        {
            std::lock_guard<std::mutex> lock(m_sessions_mutex);
            auto session_it = m_sessions.find(session_id);
            if (session_it != m_sessions.end()) {
                session_it->second.connected_to_namespace = false;
            }
        }
        
        LOG_DEBUG("Socket.IO DISCONNECT from namespace: ", nsp.empty() ? "/" : nsp, 
                  " for session: ", session_id);
    }
}

void SocketIOServer::handle_socket_io_event(const std::string& session_id, const std::string& nsp, 
                           const std::string& data) {
    // Convert empty namespace to default namespace
    std::string actual_nsp = nsp.empty() ? "/" : nsp;
    
    std::shared_ptr<SocketNamespace> socket_namespace;
    {
        std::lock_guard<std::mutex> lock(m_namespaces_mutex);
        auto ns_it = m_namespaces.find(actual_nsp);
        if (ns_it != m_namespaces.end()) {
            socket_namespace = ns_it->second;
        }
    }
    
    if (socket_namespace) {
        Message message = {false, "", socket_io::EVENT, 0, false, actual_nsp, data};
        socket_namespace->on_session_event(session_id, message);
        
        LOG_TRACE("Socket.IO EVENT in namespace: ", actual_nsp, 
                  " for session: ", session_id);
    } else {
        LOG_WARN("Socket.IO EVENT for unknown namespace: ", actual_nsp, " from session: ", session_id);
    }
}

} // namespace socketio
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE
