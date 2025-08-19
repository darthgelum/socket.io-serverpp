#ifndef SOCKETIO_SERVERPP_SOCKET_V2_HPP
#define SOCKETIO_SERVERPP_SOCKET_V2_HPP

#include "../Constants.hpp"
#include "../Error.hpp"
#include "../Event.hpp"
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
#include "../lib/rapidjson/stringbuffer.h"
#include "../lib/rapidjson/writer.h"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

// Forward declarations
namespace socketio {
    class SocketIOServer;
}
class SocketNamespace;

/**
 * @brief Updated Socket class for layered architecture
 */
class Socket {
    friend class SocketNamespace;
    friend class socketio::SocketIOServer;

public:
    /**
     * @brief Constructs a new Socket instance
     * @param server Reference to Socket.IO server
     * @param nsp Namespace for this socket
     * @param session_id Engine.IO session ID
     */
    Socket(socketio::SocketIOServer& server, const std::string& nsp, const std::string& session_id)
        : m_server(server), m_namespace(nsp), m_session_id(session_id) {
        LOG_DEBUG("Socket created for namespace: ", nsp, ", session: ", session_id);
    }

    /**
     * @brief Registers an event handler
     * @param event Event name to listen for
     * @param cb Callback function to execute when event is received
     */
    void on(const std::string& event, std::function<void(const Event& data)> cb) {
        if (event.empty()) {
            LOG_WARN("Attempting to register handler for empty event name");
            return;
        }
        std::lock_guard<std::mutex> lock(m_events_mutex);
        m_events[event] = cb;
        LOG_DEBUG("Registered event handler for: ", event, " in session: ", m_session_id);
    }

    /**
     * @brief Gets the socket ID (session ID)
     * @return Socket ID
     */
    const std::string& get_id() const {
        return m_session_id;
    }

    /**
     * @brief Sends raw data to the socket
     * @param data Data to send
     */
    void send(const std::string& data) {
        if (data.empty()) {
            LOG_WARN("Attempting to send empty data");
            return;
        }

        // Wrap data in Socket.IO EVENT packet format
        std::string event_payload = "[\"message\",\"" + escape_json_string(data) + "\"]";
        send_socket_io_event(event_payload);
        
        LOG_TRACE("Sent data to socket in namespace: ", m_namespace, ", session: ", m_session_id);
    }

    /**
     * @brief Emits an event to the socket
     * @param event Event name
     * @param data Event data
     */
    void emit(const std::string& event, const std::string& data) {
        if (event.empty()) {
            LOG_WARN("Attempting to emit event with empty name");
            return;
        }

        // Build Socket.IO event array
        std::string event_payload;
        if (data.empty()) {
            event_payload = "[\"" + escape_json_string(event) + "\"]";
        } else {
            // Try to parse data as JSON, if it fails treat as string
            rapidjson::Document json_test;
            if (json_test.Parse<0>(data.c_str()).HasParseError()) {
                // Data is not valid JSON, treat as string
                event_payload = "[\"" + escape_json_string(event) + "\",\"" + escape_json_string(data) + "\"]";
            } else {
                // Data is valid JSON, include as-is
                event_payload = "[\"" + escape_json_string(event) + "\"," + data + "]";
            }
        }

        send_socket_io_event(event_payload);
        
        LOG_TRACE("Emitted event '", event, "' to socket in namespace: ", m_namespace, 
                  ", session: ", m_session_id);
    }

    /**
     * @brief Emits an event with JSON data
     * @param event Event name
     * @param json_data JSON data as rapidjson::Value
     */
    void emit(const std::string& event, const rapidjson::Value& json_data) {
        if (event.empty()) {
            LOG_WARN("Attempting to emit event with empty name");
            return;
        }

        // Convert JSON value to string
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        json_data.Accept(writer);
        
        std::string event_payload = "[\"" + escape_json_string(event) + "\"," + buffer.GetString() + "]";
        send_socket_io_event(event_payload);
        
        LOG_TRACE("Emitted JSON event '", event, "' to socket in namespace: ", m_namespace, 
                  ", session: ", m_session_id);
    }

    /**
     * @brief Joins a room
     * @param room Room name
     */
    void join(const std::string& room) {
        if (room.empty()) {
            LOG_WARN("Attempting to join empty room name");
            return;
        }
        
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        m_rooms.insert(room);
        LOG_DEBUG("Socket joined room: ", room, " in namespace: ", m_namespace);
        
        // TODO: Notify room manager when implemented
    }

    /**
     * @brief Leaves a room
     * @param room Room name
     */
    void leave(const std::string& room) {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        auto removed = m_rooms.erase(room);
        if (removed > 0) {
            LOG_DEBUG("Socket left room: ", room, " in namespace: ", m_namespace);
            // TODO: Notify room manager when implemented
        }
    }

    /**
     * @brief Gets all rooms this socket has joined
     * @return Set of room names
     */
    std::unordered_set<std::string> rooms() const {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        return m_rooms;
    }

    /**
     * @brief Disconnects the socket
     * @param reason Disconnect reason
     */
    void disconnect(const std::string& reason = "Server disconnect") {
        // Send Socket.IO DISCONNECT packet
        send_socket_io_packet(socket_io::DISCONNECT, "");
        
        // Close the Engine.IO session
        close_session(reason);
        
        LOG_DEBUG("Socket disconnected from namespace: ", m_namespace, 
                  ", session: ", m_session_id, " - ", reason);
    }

    /**
     * @brief Gets the namespace this socket belongs to
     * @return Namespace name
     */
    const std::string& get_namespace() const {
        return m_namespace;
    }

    /**
     * @brief Gets the Engine.IO session ID
     * @return Session ID
     */
    const std::string& get_session_id() const {
        return m_session_id;
    }

    /**
     * @brief Checks if socket is connected
     * @return true if connected
     */
    bool is_connected() const {
        return m_connected;
    }

private:
    /**
     * @brief Called when a message is received
     * @param msg Message from client
     */
    void onMessage(const Message& msg);

    /**
     * @brief Called when an event is received
     * @param event_name Event name
     * @param json_data JSON data
     * @param raw_data Raw data string
     */
    void onEvent(const std::string& event_name, const rapidjson::Document& json_data, 
                const std::string& raw_data);

    /**
     * @brief Sends a Socket.IO packet
     * @param packet_type Socket.IO packet type
     * @param data Packet data
     */
    void send_socket_io_packet(int packet_type, const std::string& data);

    /**
     * @brief Sends a Socket.IO EVENT packet
     * @param event_payload Event payload (JSON array)
     */
    void send_socket_io_event(const std::string& event_payload);

    /**
     * @brief Closes the underlying Engine.IO session
     * @param reason Close reason
     */
    void close_session(const std::string& reason);

    /**
     * @brief Escapes a string for JSON
     * @param str String to escape
     * @return Escaped string
     */
    std::string escape_json_string(const std::string& str);

public:
    /**
     * @brief Sets connection state (for internal use)
     * @param connected Connection state
     */
    void set_connected(bool connected) {
        m_connected = connected;
    }

private:
    socketio::SocketIOServer& m_server;
    const std::string m_namespace;
    const std::string m_session_id;
    bool m_connected = true;

    mutable std::mutex m_events_mutex;
    std::unordered_map<std::string, std::function<void(const Event&)>> m_events;

    mutable std::mutex m_rooms_mutex;
    std::unordered_set<std::string> m_rooms;
};

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_SOCKET_V2_HPP
