#include "Socket.hpp"
#include "SocketIOServer.hpp"
#include "../Event.hpp"
#include "../lib/rapidjson/stringbuffer.h"
#include "../lib/rapidjson/writer.h"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

void Socket::onMessage(const Message& msg) {
    try {
        // For now, treat all messages as events
        if (msg.type == socket_io::EVENT && !msg.data.empty()) {
            rapidjson::Document json;
            json.Parse<0>(msg.data.c_str());
            
            if (!json.HasParseError() && json.IsArray() && json.Size() >= 1) {
                std::string event_name;
                if (json[0u].IsString()) {
                    event_name = json[0u].GetString();
                    onEvent(event_name, json, msg.data);
                } else {
                    LOG_WARN("Invalid event format - first element not string");
                }
            } else {
                LOG_WARN("Invalid JSON event data received");
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing message: ", e.what());
    }
}

void Socket::onEvent(const std::string& event_name, const rapidjson::Document& json_data, 
                    const std::string& raw_data) {
    try {
        std::function<void(const Event&)> handler;
        
        {
            std::lock_guard<std::mutex> lock(m_events_mutex);
            auto it = m_events.find(event_name);
            if (it != m_events.end()) {
                handler = it->second;
            }
        }
        
        if (handler) {
            // Create Event object with JSON data
            Event event(event_name, json_data, raw_data);
            
            // Call handler
            handler(event);
            
            LOG_TRACE("Handled event '", event_name, "' for socket in namespace: ", m_namespace);
        } else {
            LOG_DEBUG("No handler for event '", event_name, "' in namespace: ", m_namespace);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error handling event '", event_name, "': ", e.what());
    }
}

void Socket::send_socket_io_packet(int packet_type, const std::string& data) {
    try {
        bool success = m_server.send_socket_io_message(m_session_id, packet_type, m_namespace, data);
        if (!success) {
            LOG_WARN("Failed to send Socket.IO packet to session: ", m_session_id);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending Socket.IO packet: ", e.what());
    }
}

void Socket::send_socket_io_event(const std::string& event_payload) {
    send_socket_io_packet(socket_io::EVENT, event_payload);
}

void Socket::close_session(const std::string& reason) {
    try {
        m_server.close_session(m_session_id, reason);
        m_connected = false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error closing session: ", e.what());
    }
}

std::string Socket::escape_json_string(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.length() + 10); // Reserve some extra space for escapes
    
    for (char c : str) {
        switch (c) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (c < 0x20) {
                // Control character, escape as unicode
                char buffer[7];
                snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                escaped += buffer;
            } else {
                escaped += c;
            }
            break;
        }
    }
    
    return escaped;
}

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE
