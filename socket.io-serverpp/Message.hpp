#ifndef SOCKETIO_SERVERPP_MESSAGE_HPP
#define SOCKETIO_SERVERPP_MESSAGE_HPP

#include "config.hpp"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

/**
 * @brief Represents a Socket.IO message with metadata
 */
struct Message {
    bool isJson;              ///< Whether the message contains JSON data
    string sender;            ///< Sender identifier
    int type;                 ///< Message type (Socket.IO packet type)
    int messageId;            ///< Message ID for acknowledgments
    bool idHandledByUser;     ///< Whether the message ID is handled by user code
    string endpoint;          ///< Namespace endpoint
    string data;              ///< Message payload data

    /**
     * @brief Default constructor
     */
    Message() 
        : isJson(false), type(0), messageId(0), idHandledByUser(false) {}

    /**
     * @brief Constructs a Message with all parameters
     */
    Message(bool json, const string& from, int msg_type, int id, 
            bool user_handled, const string& ep, const string& payload)
        : isJson(json), sender(from), type(msg_type), messageId(id),
          idHandledByUser(user_handled), endpoint(ep), data(payload) {}

    /**
     * @brief Checks if the message is empty
     * @return true if data is empty, false otherwise
     */
    bool empty() const {
        return data.empty();
    }

    /**
     * @brief Gets the size of the message data
     * @return Data size in bytes
     */
    size_t size() const {
        return data.size();
    }

    /**
     * @brief Checks if this is an acknowledgment message
     * @return true if messageId > 0, false otherwise
     */
    bool is_ack() const {
        return messageId > 0;
    }
};

} // namespace lib

using lib::Message;

} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_MESSAGE_HPP
