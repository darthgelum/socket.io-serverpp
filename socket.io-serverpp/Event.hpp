#ifndef SOCKETIO_SERVERPP_EVENT_HPP
#define SOCKETIO_SERVERPP_EVENT_HPP

#include "Error.hpp"
#include "Message.hpp"
#include "config.hpp"

#include "lib/rapidjson/document.h"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

/**
 * @brief Represents a Socket.IO event with data
 */
class Event {
public:
    /**
     * @brief Constructs an Event with JSON data
     * @param event Event name
     * @param json Parsed JSON document
     * @param rawJson Raw JSON string
     */
    Event(const string& event, const rapidjson::Document& json, const string& rawJson)
        : m_isJson(true), m_event(event), m_json(&json), m_stringdata(rawJson) {
        if (event.empty()) {
            throw SocketIOException("Event name cannot be empty", SocketIOErrorCode::INVALID_NAMESPACE);
        }
    }
    
    /**
     * @brief Constructs an Event with string data
     * @param event Event name
     * @param data String data
     */
    Event(const string& event, const string& data)
        : m_isJson(false), m_event(event), m_json(nullptr), m_stringdata(data) {
        if (event.empty()) {
            throw SocketIOException("Event name cannot be empty", SocketIOErrorCode::INVALID_NAMESPACE);
        }
    }

    /**
     * @brief Checks if the event contains JSON data
     * @return true if JSON, false if string
     */
    bool isJson() const {
        return m_isJson;
    }

    /**
     * @brief Gets the event name
     * @return Event name string
     */
    const string& name() const {
        return m_event;
    }

    /**
     * @brief Gets the raw data as string
     * @return Data string
     */
    const string& data() const {
        return m_stringdata;
    }

    /**
     * @brief Gets the parsed JSON document (only valid if isJson() returns true)
     * @return Pointer to JSON document or nullptr
     */
    const rapidjson::Document* json() const {
        return m_json;
    }

    /**
     * @brief Checks if the event data is empty
     * @return true if empty, false otherwise
     */
    bool empty() const {
        return m_stringdata.empty();
    }

    /**
     * @brief Gets the size of the data
     * @return Data size in bytes
     */
    size_t size() const {
        return m_stringdata.size();
    }

private:
    bool m_isJson;
    string m_event;
    const rapidjson::Document* m_json;
    string m_stringdata;
};

}

using lib::Event;

}

#endif // SOCKETIO_SERVERPP_EVENT_HPP
