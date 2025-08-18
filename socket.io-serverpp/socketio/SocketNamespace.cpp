#include "SocketNamespace.hpp"
#include "SocketIOServer.hpp"
#include "Socket.hpp"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

void SocketNamespace::send(const std::string& data) {
    if (data.empty()) {
        LOG_WARN("Attempting to send empty data");
        return;
    }

    size_t sent_count = 0;
    {
        std::lock_guard<std::mutex> lock(m_sockets_mutex);
        for (const auto& socket_pair : m_sockets) {
            try {
                socket_pair.second->send(data);
                ++sent_count;
            } catch (const std::exception& e) {
                LOG_WARN("Failed to send to socket: ", e.what());
            }
        }
    }
    LOG_DEBUG("Sent data to ", sent_count, " sockets in namespace: ", m_namespace);
}

void SocketNamespace::emit(const std::string& name, const std::string& data) {
    if (name.empty()) {
        LOG_WARN("Attempting to emit event with empty name");
        return;
    }

    size_t sent_count = 0;
    {
        std::lock_guard<std::mutex> lock(m_sockets_mutex);
        for (const auto& socket_pair : m_sockets) {
            try {
                socket_pair.second->emit(name, data);
                ++sent_count;
            } catch (const std::exception& e) {
                LOG_WARN("Failed to emit to socket: ", e.what());
            }
        }
    }
    LOG_DEBUG("Emitted event '", name, "' to ", sent_count, " sockets in namespace: ", m_namespace);
}

void SocketNamespace::broadcast_from_session(const std::string& sender_session_id, 
                                            const std::string& event_data) {
    size_t broadcast_count = 0;
    
    std::lock_guard<std::mutex> lock(m_sockets_mutex);
    for (const auto& socket_pair : m_sockets) {
        // Skip sender
        if (socket_pair.first == sender_session_id) {
            continue;
        }
        
        try {
            // Send Socket.IO EVENT packet
            bool success = m_server.send_socket_io_message(
                socket_pair.first, 
                socket_io::EVENT, 
                m_namespace, 
                event_data
            );
            
            if (success) {
                ++broadcast_count;
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to broadcast to session ", socket_pair.first, ": ", e.what());
        }
    }
    
    LOG_TRACE("Broadcast event to ", broadcast_count, " sessions in namespace: ", m_namespace);
}

void SocketNamespace::on_session_connect(const std::string& session_id) {
    try {
        auto socket = create_socket(session_id);
        if (socket) {
            {
                std::lock_guard<std::mutex> lock(m_sockets_mutex);
                m_sockets[session_id] = socket;
            }
            
            call_connection_handlers(*socket);
            
            LOG_DEBUG("Session connected to namespace: ", m_namespace, 
                      " (total: ", socket_count(), ")");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error handling session connection: ", e.what());
    }
}

void SocketNamespace::on_session_disconnect(const std::string& session_id) {
    std::shared_ptr<Socket> socket;
    
    {
        std::lock_guard<std::mutex> lock(m_sockets_mutex);
        auto it = m_sockets.find(session_id);
        if (it != m_sockets.end()) {
            socket = it->second;
            m_sockets.erase(it);
        }
    }
    
    if (socket) {
        try {
            socket->set_connected(false);
            call_disconnection_handlers(*socket);
            
            LOG_DEBUG("Session disconnected from namespace: ", m_namespace, 
                      " (remaining: ", socket_count(), ")");
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling session disconnection: ", e.what());
        }
    }
}

void SocketNamespace::on_session_event(const std::string& session_id, const Message& message) {
    std::shared_ptr<Socket> socket;
    
    {
        std::lock_guard<std::mutex> lock(m_sockets_mutex);
        auto it = m_sockets.find(session_id);
        if (it != m_sockets.end()) {
            socket = it->second;
        }
    }
    
    if (socket) {
        try {
            socket->onMessage(message);
            
            // Broadcast to other sockets in the namespace
            broadcast_from_session(session_id, message.data);
            
            LOG_TRACE("Processed event from session: ", session_id, " in namespace: ", m_namespace);
        } catch (const std::exception& e) {
            LOG_ERROR("Error processing session event: ", e.what());
        }
    } else {
        LOG_WARN("Event received from unknown session: ", session_id, " in namespace: ", m_namespace);
    }
}

std::shared_ptr<Socket> SocketNamespace::create_socket(const std::string& session_id) {
    try {
        return std::make_shared<Socket>(m_server, m_namespace, session_id);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create socket for session ", session_id, ": ", e.what());
        return nullptr;
    }
}

void SocketNamespace::remove_socket(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(m_sockets_mutex);
    m_sockets.erase(session_id);
}

void SocketNamespace::call_connection_handlers(Socket& socket) {
    try {
        std::lock_guard<std::mutex> lock(m_handlers_mutex);
        for (const auto& handler : m_connection_handlers) {
            try {
                handler(socket);
            } catch (const std::exception& e) {
                LOG_ERROR("Error in connection handler: ", e.what());
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error calling connection handlers: ", e.what());
    }
}

void SocketNamespace::call_disconnection_handlers(Socket& socket) {
    try {
        std::lock_guard<std::mutex> lock(m_handlers_mutex);
        for (const auto& handler : m_disconnection_handlers) {
            try {
                handler(socket);
            } catch (const std::exception& e) {
                LOG_ERROR("Error in disconnection handler: ", e.what());
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error calling disconnection handlers: ", e.what());
    }
}

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE
