#ifndef SOCKETIO_SERVERPP_SOCKET_HPP
#define SOCKETIO_SERVERPP_SOCKET_HPP

#include "Constants.hpp"
#include "Error.hpp"
#include "Event.hpp"
#include "Logger.hpp"
#include "Message.hpp"
#include "config.hpp"

#include "lib/rapidjson/document.h"

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class SocketNamespace;

/**
 * @brief Represents a Socket.IO socket connection
 */
class Socket {
    friend SocketNamespace;
    
public:
    /**
     * @brief Constructs a new Socket instance
     * @param wsserver WebSocket server reference
     * @param nsp Namespace for this socket
     * @param hdl WebSocket connection handle
     */
    Socket(wsserver& wsserver, const string& nsp, wspp::connection_hdl hdl)
        : m_wsserver(wsserver), m_namespace(nsp), m_ws_hdl(hdl) {
        LOG_DEBUG("Socket created for namespace: ", nsp);
    }

    /**
     * @brief Registers an event handler
     * @param event Event name to listen for
     * @param cb Callback function to execute when event is received
     */
    void on(const string& event, std::function<void(const Event& data)> cb) {
        if (event.empty()) {
            LOG_WARN("Attempting to register handler for empty event name");
            return;
        }
        m_events[event] = cb;
        LOG_DEBUG("Registered event handler for: ", event);
    }

    /**
     * @brief Sends raw data to the client
     * @param data JSON data to send (must be valid JSON)
     */
    void send(const string& data) {
        try {
            // Socket.IO v5: EVENT packet with raw payload array or string
            string payload = build_socket_io_frame(socket_io::EVENT);
            payload += data; // caller must pass a valid JSON (e.g. ["event", ...])
            
            m_wsserver.send(m_ws_hdl, payload, wspp::frame::opcode::value::text);
            LOG_TRACE("Socket sent data: ", data);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to send data: ", e.what());
            throw SocketIOException("Failed to send data", SocketIOErrorCode::CONNECTION_FAILED);
        }
    }

    /**
     * @brief Emits an event with data to the client
     * @param name Event name
     * @param data Event data (must be valid JSON)
     */
    void emit(const string& name, const string& data) {
        try {
            // Socket.IO v5: EVENT packet with [name, ...args]
            string payload = build_socket_io_frame(socket_io::EVENT);
            payload += "[\"" + name + "\"," + data + "]";
            
            m_wsserver.send(m_ws_hdl, payload, wspp::frame::opcode::value::text);
            LOG_TRACE("Socket emitted event: ", name, " with data: ", data);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to emit event: ", e.what());
            throw SocketIOException("Failed to emit event", SocketIOErrorCode::CONNECTION_FAILED);
        }
    }

    /**
     * @brief Gets the unique identifier for this socket connection
     * @return UUID string
     */
    string uuid() const {
        try {
            auto connection = m_wsserver.get_con_from_hdl(m_ws_hdl);
            string resource = connection->get_resource();
            // Extract UUID from resource path (format: /socket.io/?...&sid=UUID)
            const string sid_param = "sid=";
            size_t sid_pos = resource.find(sid_param);
            if (sid_pos != string::npos) {
                size_t start = sid_pos + sid_param.length();
                size_t end = resource.find('&', start);
                if (end == string::npos) {
                    return resource.substr(start);
                }
                return resource.substr(start, end - start);
            }
            // Fallback: extract from path (legacy format)
            return resource.substr(23);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get socket UUID: ", e.what());
            return "";
        }
    }

    /**
     * @brief Gets the namespace this socket belongs to
     * @return Namespace string
     */
    const string& get_namespace() const {
        return m_namespace;
    }

    /**
     * @brief Checks if the socket connection is still active
     * @return true if active, false otherwise
     */
    bool is_connected() const {
        try {
            auto connection = m_wsserver.get_con_from_hdl(m_ws_hdl);
            return connection && connection->get_state() == websocketpp::session::state::open;
        } catch (const std::exception& e) {
            LOG_WARN("Error checking connection state: ", e.what());
            return false;
        }
    }

private:
    /**
     * @brief Handles incoming message events
     * @param msg Message to process
     */
    void onMessage(const Message& msg) {
        auto iter = m_events.find("message");
        if (iter != m_events.end()) {
            try {
                iter->second({"message", msg.data});
                LOG_TRACE("Handled message event");
            } catch (const std::exception& e) {
                LOG_ERROR("Error in message handler: ", e.what());
            }
        }
    }

    /**
     * @brief Handles incoming custom events
     * @param event Event name
     * @param json Parsed JSON document
     * @param rawJson Raw JSON string
     */
    void onEvent(const string& event, const rapidjson::Document& json, const string& rawJson) {
        auto iter = m_events.find(event);
        if (iter != m_events.end()) {
            try {
                iter->second({event, json, rawJson});
                LOG_TRACE("Handled event: ", event);
            } catch (const std::exception& e) {
                LOG_ERROR("Error in event handler for '", event, "': ", e.what());
            }
        } else {
            LOG_DEBUG("No handler for event: ", event);
        }
    }

    /**
     * @brief Builds a Socket.IO frame with Engine.IO wrapper
     * @param socket_io_type Socket.IO packet type
     * @return Formatted frame string
     */
    string build_socket_io_frame(int socket_io_type) const {
        string frame;
        frame += engine_io::MESSAGE; // Engine.IO message wrapper
        frame += std::to_string(socket_io_type); // Socket.IO packet type
        
        if (!m_namespace.empty()) {
            frame += m_namespace + ",";
        }
        
        return frame;
    }

    // Member variables
    wsserver& m_wsserver;
    const string m_namespace;
    wspp::connection_hdl m_ws_hdl;
    map<string, std::function<void(const Event&)>> m_events;
};

}
    using lib::Socket;
}
#endif