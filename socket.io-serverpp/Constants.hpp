#ifndef SOCKETIO_SERVERPP_CONSTANTS_HPP
#define SOCKETIO_SERVERPP_CONSTANTS_HPP

#include "config.hpp"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace constants {

// Engine.IO packet types
namespace engine_io {
    constexpr char OPEN = '0';
    constexpr char CLOSE = '1';
    constexpr char PING = '2';
    constexpr char PONG = '3';
    constexpr char MESSAGE = '4';
}

// Socket.IO packet types
namespace socket_io {
    constexpr int CONNECT = 0;
    constexpr int DISCONNECT = 1;
    constexpr int EVENT = 2;
    constexpr int ACK = 3;
    constexpr int CONNECT_ERROR = 4;
    constexpr int BINARY_EVENT = 5;
    constexpr int BINARY_ACK = 6;
}

// Protocol constants
namespace protocol {
    constexpr const char* PROBE_PING = "2probe";
    constexpr const char* PROBE_PONG = "3probe";
    constexpr const char* ENGINE_IO_PING = "2";
    constexpr const char* ENGINE_IO_PONG = "3";
}

// Default timeout values (milliseconds)
namespace timeouts {
    constexpr int DEFAULT_PING_INTERVAL = 25000;
    constexpr int DEFAULT_PING_TIMEOUT = 5000;
    constexpr int DEFAULT_HEARTBEAT = 30;
    constexpr int DEFAULT_CLOSE_TIME = 30;
    constexpr int DEFAULT_MAX_PAYLOAD = 1000000;
}

// HTTP status codes
namespace http_status {
    constexpr const char* OK = "200 OK";
    constexpr const char* BAD_REQUEST = "400 Bad Request";
    constexpr const char* NOT_FOUND = "404 Not Found";
    constexpr const char* INTERNAL_ERROR = "500 Internal Server Error";
}

} // namespace constants

// Bring constants into lib namespace for easier access
using namespace constants;

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_CONSTANTS_HPP
