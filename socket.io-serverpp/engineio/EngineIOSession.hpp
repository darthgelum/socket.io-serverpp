#ifndef SOCKETIO_SERVERPP_ENGINE_IO_SESSION_HPP
#define SOCKETIO_SERVERPP_ENGINE_IO_SESSION_HPP

#include "../config.hpp"
#include "../Constants.hpp"
#include "../Logger.hpp"
#include "../transport/Transport.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace engineio {

/**
 * @brief Engine.IO session state
 */
enum class SessionState {
    CONNECTING,
    OPEN,
    CLOSING,
    CLOSED
};

/**
 * @brief Engine.IO packet types (extended from constants)
 */
namespace packet_type {
    using namespace engine_io;
    constexpr char UPGRADE = '5';
    constexpr char NOOP = '6';
}

/**
 * @brief Engine.IO session configuration
 */
struct EngineIOConfig {
    int ping_interval = timeouts::DEFAULT_PING_INTERVAL;    // 25000ms
    int ping_timeout = timeouts::DEFAULT_PING_TIMEOUT;      // 20000ms (v4 typical)
    int max_payload = timeouts::DEFAULT_MAX_PAYLOAD;        // 1MB
    bool allow_upgrades = true;                             // advertise websocket upgrade
    
    EngineIOConfig() = default;
};

/**
 * @brief Forward declaration for session handler
 */
class EngineIOSessionHandler {
public:
    virtual ~EngineIOSessionHandler() = default;
    
    /**
     * @brief Called when session is established
     * @param session_id Session identifier
     */
    virtual void on_session_open(const std::string& session_id) = 0;
    
    /**
     * @brief Called when a message is received
     * @param session_id Session identifier
     * @param message Message payload
     */
    virtual void on_session_message(const std::string& session_id, const std::string& message) = 0;
    
    /**
     * @brief Called when session is closed
     * @param session_id Session identifier
     * @param reason Close reason
     */
    virtual void on_session_close(const std::string& session_id, const std::string& reason) = 0;
    
    /**
     * @brief Called when session error occurs
     * @param session_id Session identifier
     * @param error Error description
     */
    virtual void on_session_error(const std::string& session_id, const std::string& error) = 0;
};

/**
 * @brief Engine.IO session managing protocol state
 */
class EngineIOSession {
public:
    /**
     * @brief Constructs a new Engine.IO session
     * @param session_id Unique session identifier
     * @param connection Transport connection
     * @param config Session configuration
     * @param handler Session event handler
     */
    EngineIOSession(const std::string& session_id,
                   const transport::ConnectionHandle& connection,
                   const EngineIOConfig& config,
                   std::shared_ptr<EngineIOSessionHandler> handler)
        : m_session_id(session_id)
        , m_connection(connection)
        , m_config(config)
        , m_handler(handler)
        , m_state(SessionState::CONNECTING)
        , m_transport_closed(false)
        , m_last_pong(std::chrono::steady_clock::now())
        , m_ping_timer(nullptr) {
        
        LOG_DEBUG("Engine.IO session created: ", session_id);
    }
    
    ~EngineIOSession() {
        close("Session destroyed");
    }
    
    /**
     * @brief Starts the session with handshake
     * @param transport Transport to use for sending
     */
    void start(std::shared_ptr<transport::Transport> transport) {
        if (m_state != SessionState::CONNECTING) {
            LOG_WARN("Attempting to start session in invalid state");
            return;
        }
        
        m_transport = transport;
        m_state = SessionState::OPEN;
        
        // Send Engine.IO OPEN packet with handshake data
        send_handshake();
        
        // Start heartbeat
        schedule_ping();
        
        // Notify handler
        if (m_handler) {
            m_handler->on_session_open(m_session_id);
        }
        
        LOG_DEBUG("Engine.IO session started: ", m_session_id);
    }
    
    /**
     * @brief Processes incoming raw message
     * @param payload Raw message payload
     * @return true if message was handled, false if it should be passed up
     */
    bool process_message(const std::string& payload) {
        if (payload.empty()) {
            LOG_WARN("Empty message received for session: ", m_session_id);
            return false;
        }
        
        char packet_type = payload[0];
        std::string packet_data = payload.length() > 1 ? payload.substr(1) : std::string();
        
        LOG_TRACE("Processing Engine.IO packet type: ", packet_type, " for session: ", m_session_id);
        
        switch (packet_type) {
        case engine_io::PING:
            handle_ping(packet_data);
            return true;
            
        case engine_io::PONG:
            handle_pong();
            return true;
            
        case engine_io::CLOSE:
            handle_close();
            return true;
            
        case engine_io::MESSAGE:
            // Pass message content to higher layer (Socket.IO)
            if (m_handler) {
                m_handler->on_session_message(m_session_id, packet_data);
            }
            return false; // Let higher layer handle the actual message
            
        case packet_type::UPGRADE:
            handle_upgrade(packet_data);
            return true;
            
        case packet_type::NOOP:
            // No operation - just acknowledge receipt
            return true;
            
        default:
            LOG_WARN("Unknown Engine.IO packet type: ", packet_type, " for session: ", m_session_id);
            return false;
        }
    }
    
    /**
     * @brief Sends a message through this session
     * @param message Message payload to send
     * @return true if sent successfully
     */
    bool send_message(const std::string& message) {
        if (m_state != SessionState::OPEN) {
            LOG_WARN("Attempting to send on non-open session: ", m_session_id);
            return false;
        }
        
        auto transport = m_transport.lock();
        if (!transport) {
            LOG_WARN("Transport unavailable for session: ", m_session_id);
            return false;
        }
        
        std::string packet = std::string(1, engine_io::MESSAGE) + message;
        bool success = transport->send_message(m_connection, packet);
        
        if (success) {
            LOG_TRACE("Sent message through session: ", m_session_id);
        } else {
            LOG_WARN("Failed to send message through session: ", m_session_id);
        }
        
        return success;
    }
    
    /**
     * @brief Closes the session
     * @param reason Close reason
     */
    void close(const std::string& reason = "Normal close") {
        if (m_state == SessionState::CLOSED || m_state == SessionState::CLOSING) {
            return;
        }
        
        m_state = SessionState::CLOSING;
        
        // Cancel ping timer
        cancel_ping_timer();
        
        // Only try to send close packet and close transport if transport isn't already closed
        if (!m_transport_closed) {
            auto transport = m_transport.lock();
            if (transport) {
                std::string close_packet(1, engine_io::CLOSE);
                bool sent = transport->send_message(m_connection, close_packet);
                if (!sent) {
                    LOG_DEBUG("Could not send close packet for session ", m_session_id, " - transport likely closed");
                }
                
                // Close transport connection
                transport->close_connection(m_connection, 1000, reason);
            }
        } else {
            LOG_DEBUG("Skipping transport close for session ", m_session_id, " - already closed by transport");
        }
        
        m_state = SessionState::CLOSED;
        
        // Notify handler
        if (m_handler) {
            m_handler->on_session_close(m_session_id, reason);
        }
        
        LOG_DEBUG("Engine.IO session closed: ", m_session_id, " - ", reason);
    }
    
    /**
     * @brief Marks the transport as closed externally
     * This should be called when the transport connection is closed
     * at the transport layer (e.g., WebSocket disconnect)
     */
    void mark_transport_closed() {
        m_transport_closed = true;
        LOG_DEBUG("Transport marked as closed for session: ", m_session_id);
    }
    
    /**
     * @brief Gets the session ID
     * @return Session identifier
     */
    const std::string& get_session_id() const {
        return m_session_id;
    }
    
    /**
     * @brief Gets the session state
     * @return Current session state
     */
    SessionState get_state() const {
        return m_state;
    }
    
    /**
     * @brief Gets the connection handle
     * @return Transport connection handle
     */
    const transport::ConnectionHandle& get_connection() const {
        return m_connection;
    }
    
    /**
     * @brief Sets ping timer for external timer management
     * @param timer Timer instance
     */
    void set_ping_timer(std::shared_ptr<asio::steady_timer> timer) {
        m_ping_timer = timer;
    }

private:
    void send_handshake() {
        auto transport = m_transport.lock();
        if (!transport) {
            LOG_ERROR("No transport available for handshake");
            return;
        }
        
        // Build Engine.IO OPEN packet with handshake data
        std::ostringstream handshake;
        handshake << engine_io::OPEN;
        handshake << "{\"sid\":\"" << m_session_id << "\",";
        // Advertise upgrades if allowed
        if (m_config.allow_upgrades) {
            handshake << "\"upgrades\":[\"websocket\"],";
        } else {
            handshake << "\"upgrades\":[],";
        }
        handshake << "\"pingInterval\":" << m_config.ping_interval << ",";
        handshake << "\"pingTimeout\":" << m_config.ping_timeout << ",";
        handshake << "\"maxPayload\":" << m_config.max_payload << "}";
        
        transport->send_message(m_connection, handshake.str());
        
        LOG_DEBUG("Sent Engine.IO handshake for session: ", m_session_id);
    }
    
    void handle_ping(const std::string& data) {
        auto transport = m_transport.lock();
        if (!transport) {
            LOG_WARN("No transport available for pong response");
            return;
        }
        
        // Special handling for upgrade probe
        if (data == "probe") {
            std::string pong_packet = std::string(1, engine_io::PONG) + "probe";
            transport->send_message(m_connection, pong_packet);
            LOG_TRACE("Responded to ping probe for session: ", m_session_id);
        } else {
            // Regular ping - respond with pong
            std::string pong_packet(1, engine_io::PONG);
            transport->send_message(m_connection, pong_packet);
            LOG_TRACE("Responded to ping for session: ", m_session_id);
        }
        
        // Update last activity
        m_last_pong = std::chrono::steady_clock::now();
    }
    
    void handle_pong() {
        m_last_pong = std::chrono::steady_clock::now();
        LOG_TRACE("Received pong for session: ", m_session_id);
    }
    
    void handle_close() {
        LOG_DEBUG("Received close packet for session: ", m_session_id);
        close("Client requested close");
    }
    
    void handle_upgrade(const std::string& data) {
        // For now, we don't support transport upgrades (WebSocket only)
        LOG_DEBUG("Ignoring upgrade request for session: ", m_session_id);
    }
    
    void schedule_ping() {
        auto timer = m_ping_timer;
        if (!timer) {
            LOG_WARN("No ping timer available for session: ", m_session_id);
            return;
        }
        
        timer->expires_after(std::chrono::milliseconds(m_config.ping_interval));
        timer->async_wait([this, session_id = m_session_id](const boost::system::error_code& ec) {
            if (ec || m_state != SessionState::OPEN) {
                return; // Timer cancelled or session closed
            }
            
            handle_ping_timeout();
        });
    }
    
    void handle_ping_timeout() {
        // Check if client is still responsive
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_pong).count();
        
        if (elapsed > (m_config.ping_interval + m_config.ping_timeout)) {
            LOG_INFO("Session ping timeout, closing: ", m_session_id);
            close("Ping timeout");
            return;
        }
        
        // Send ping
        auto transport = m_transport.lock();
        if (transport) {
            std::string ping_packet(1, engine_io::PING);
            if (transport->send_message(m_connection, ping_packet)) {
                LOG_TRACE("Sent ping to session: ", m_session_id);
                // Schedule next ping
                schedule_ping();
            } else {
                LOG_WARN("Failed to send ping to session: ", m_session_id);
                close("Failed to send ping");
            }
        }
    }
    
    void cancel_ping_timer() {
        auto timer = m_ping_timer;
        if (timer) {
            boost::system::error_code ec;
            timer->cancel(ec);
            if (ec) {
                LOG_WARN("Error canceling ping timer for session ", m_session_id, ": ", ec.message());
            }
        }
    }

private:
    std::string m_session_id;
    transport::ConnectionHandle m_connection;
    EngineIOConfig m_config;
    std::shared_ptr<EngineIOSessionHandler> m_handler;
    std::weak_ptr<transport::Transport> m_transport;
    
    SessionState m_state;
    bool m_transport_closed;  // Flag to track if transport was closed externally
    std::chrono::steady_clock::time_point m_last_pong;
    std::shared_ptr<asio::steady_timer> m_ping_timer;
};

} // namespace engineio
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_ENGINE_IO_SESSION_HPP
