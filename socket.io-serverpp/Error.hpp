#ifndef SOCKETIO_SERVERPP_ERROR_HPP
#define SOCKETIO_SERVERPP_ERROR_HPP

#include "config.hpp"
#include <system_error>
#include <stdexcept>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

// Custom error codes for Socket.IO operations
enum class SocketIOErrorCode {
    SUCCESS = 0,
    INVALID_NAMESPACE,
    INVALID_STATE,
    CONNECTION_FAILED,
    MESSAGE_PARSE_ERROR,
    TIMEOUT,
    PROTOCOL_ERROR,
    RESOURCE_EXHAUSTED
};

// Custom exception class for Socket.IO errors
class SocketIOException : public std::runtime_error {
public:
    explicit SocketIOException(const string& message, SocketIOErrorCode code = SocketIOErrorCode::PROTOCOL_ERROR)
        : std::runtime_error(message), error_code_(code) {}
    
    SocketIOErrorCode error_code() const noexcept { return error_code_; }

private:
    SocketIOErrorCode error_code_;
};

// Error category for Socket.IO error codes
class SocketIOErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "socket_io";
    }
    
    string message(int ev) const override {
        switch (static_cast<SocketIOErrorCode>(ev)) {
            case SocketIOErrorCode::SUCCESS:
                return "Success";
            case SocketIOErrorCode::INVALID_NAMESPACE:
                return "Invalid namespace";
            case SocketIOErrorCode::CONNECTION_FAILED:
                return "Connection failed";
            case SocketIOErrorCode::MESSAGE_PARSE_ERROR:
                return "Message parse error";
            case SocketIOErrorCode::TIMEOUT:
                return "Operation timeout";
            case SocketIOErrorCode::PROTOCOL_ERROR:
                return "Protocol error";
            case SocketIOErrorCode::RESOURCE_EXHAUSTED:
                return "Resource exhausted";
            default:
                return "Unknown error";
        }
    }
};

// Global error category instance
inline const SocketIOErrorCategory& socket_io_category() {
    static SocketIOErrorCategory instance;
    return instance;
}

// Helper function to create error codes
inline std::error_code make_error_code(SocketIOErrorCode e) {
    return {static_cast<int>(e), socket_io_category()};
}

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

// Enable std::error_code support for SocketIOErrorCode
namespace std {
template<>
struct is_error_code_enum<SOCKETIO_SERVERPP_NAMESPACE::lib::SocketIOErrorCode> : true_type {};
}

#endif // SOCKETIO_SERVERPP_ERROR_HPP
