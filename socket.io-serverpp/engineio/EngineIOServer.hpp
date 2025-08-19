#ifndef SOCKETIO_SERVERPP_ENGINE_IO_SERVER_HPP
#define SOCKETIO_SERVERPP_ENGINE_IO_SERVER_HPP

#include "EngineIOSession.hpp"
#include "../config.hpp"
#include "../Logger.hpp"
#include "../transport/Transport.hpp"
#include "../uuid.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {
namespace engineio {

/**
 * @brief Engine.IO server event handler
 */
class EngineIOServerHandler {
public:
    virtual ~EngineIOServerHandler() = default;
    
    /**
     * @brief Called when a new Engine.IO session is established
     * @param session_id Session identifier
     */
    virtual void on_engine_io_session_open(const std::string& session_id) = 0;
    
    /**
     * @brief Called when a message is received from a session
     * @param session_id Session identifier  
     * @param message Message content (without Engine.IO framing)
     */
    virtual void on_engine_io_message(const std::string& session_id, const std::string& message) = 0;
    
    /**
     * @brief Called when a session is closed
     * @param session_id Session identifier
     * @param reason Close reason
     */
    virtual void on_engine_io_session_close(const std::string& session_id, const std::string& reason) = 0;
    
    /**
     * @brief Called when a session error occurs
     * @param session_id Session identifier
     * @param error Error description
     */
    virtual void on_engine_io_session_error(const std::string& session_id, const std::string& error) = 0;
};

/**
 * @brief Engine.IO server managing sessions and transport
 */
class EngineIOServer : public transport::TransportEventHandler, 
                      public EngineIOSessionHandler,
                      public std::enable_shared_from_this<EngineIOServer> {
public:
    /**
     * @brief Constructs Engine.IO server
     * @param io_service IO service for async operations
     * @param config Engine.IO configuration
     */
    explicit EngineIOServer(asio::io_service& io_service, 
                           const EngineIOConfig& config = EngineIOConfig())
        : m_io_service(io_service)
        , m_config(config) {
        LOG_INFO("Engine.IO server created");
    }
    
    ~EngineIOServer() {
        stop();
    }
    
    /**
     * @brief Sets the Engine.IO event handler
     * @param handler Event handler
     */
    void set_handler(std::shared_ptr<EngineIOServerHandler> handler) {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        m_handler = handler;
    }
    
    /**
     * @brief Adds a transport to the server
     * @param transport Transport instance
     */
    void add_transport(std::shared_ptr<transport::Transport> transport) {
        std::lock_guard<std::mutex> lock(m_transports_mutex);
        transport->set_event_handler(shared_from_this());
        m_transports.push_back(transport);
        LOG_INFO("Added transport: ", transport->get_name());
    }
    
    /**
     * @brief Starts listening on the specified address and port
     * @param address Address to bind to
     * @param port Port to listen on
     */
    void listen(const std::string& address, int port) {
        std::lock_guard<std::mutex> lock(m_transports_mutex);
        for (auto& transport : m_transports) {
            transport->listen(address, port);
        }
        LOG_INFO("Engine.IO server listening on ", address, ":", port);
    }
    
    /**
     * @brief Starts accepting connections
     */
    void start_accept() {
        std::lock_guard<std::mutex> lock(m_transports_mutex);
        for (auto& transport : m_transports) {
            transport->start_accept();
        }
        LOG_DEBUG("Engine.IO server started accepting connections");
    }
    
    /**
     * @brief Sends a message to a specific session
     * @param session_id Target session
     * @param message Message to send
     * @return true if sent successfully
     */
    bool send_to_session(const std::string& session_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(m_sessions_mutex);
        auto it = m_sessions.find(session_id);
        if (it != m_sessions.end()) {
            return it->second->send_message(message);
        }
        LOG_WARN("Session not found for send: ", session_id);
        return false;
    }
    
    /**
     * @brief Closes a specific session
     * @param session_id Session to close
     * @param reason Close reason
     */
    void close_session(const std::string& session_id, const std::string& reason = "Server close") {
        std::lock_guard<std::mutex> lock(m_sessions_mutex);
        auto it = m_sessions.find(session_id);
        if (it != m_sessions.end()) {
            it->second->close(reason);
        }
    }
    
    /**
     * @brief Gets session count
     * @return Number of active sessions
     */
    size_t get_session_count() const {
        std::lock_guard<std::mutex> lock(m_sessions_mutex);
        return m_sessions.size();
    }
    
    /**
     * @brief Stops the server
     */
    void stop() {
        LOG_INFO("Stopping Engine.IO server");
        
        // Close all sessions
        {
            std::lock_guard<std::mutex> lock(m_sessions_mutex);
            for (auto& session_pair : m_sessions) {
                session_pair.second->close("Server shutdown");
            }
            m_sessions.clear();
        }
        
        // Stop all transports
        {
            std::lock_guard<std::mutex> lock(m_transports_mutex);
            for (auto& transport : m_transports) {
                transport->stop();
            }
            m_transports.clear();
        }
        
        // Cancel all timers
        {
            std::lock_guard<std::mutex> lock(m_timers_mutex);
            for (auto& timer : m_timers) {
                boost::system::error_code ec;
                timer.second->cancel(ec);
            }
            m_timers.clear();
        }
        
        LOG_INFO("Engine.IO server stopped");
    }

    // TransportEventHandler interface
    void on_connection_open(const transport::ConnectionInfo& info) override {
        try {
            // Look for existing session ID in query parameters
            std::string session_id;
            auto sid_it = info.query_params.find("sid");
            if (sid_it != info.query_params.end() && !sid_it->second.empty()) {
                session_id = sid_it->second;
                LOG_DEBUG("Reusing session ID from query: ", session_id);
            } else {
                session_id = lib::uuid::uuid1();
                LOG_DEBUG("Generated new session ID: ", session_id);
            }
            
            // Create new session
            transport::ConnectionHandle conn_handle(info.id);
            auto session = std::make_shared<EngineIOSession>(
                session_id, conn_handle, m_config, shared_from_this());
            
            // Create and assign timer for this session
            auto timer = std::make_shared<asio::steady_timer>(m_io_service);
            session->set_ping_timer(timer);
            
            // Store session and timer
            {
                std::lock_guard<std::mutex> sessions_lock(m_sessions_mutex);
                std::lock_guard<std::mutex> timers_lock(m_timers_mutex);
                
                m_sessions[session_id] = session;
                m_timers[session_id] = timer;
                m_connection_to_session[info.id] = session_id;
            }
            
            // Find appropriate transport and start session
            auto transport = find_transport_for_connection(conn_handle);
            if (transport) {
                session->start(transport);
            } else {
                LOG_ERROR("No transport found for connection: ", info.id);
                session->close("No transport available");
            }
            
            LOG_DEBUG("Engine.IO session created and started: ", session_id);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error creating Engine.IO session: ", e.what());
        }
    }
    
    void on_message(const transport::TransportMessage& message) override {
        try {
            std::string session_id = get_session_id_for_connection(message.connection.id);
            if (session_id.empty()) {
                LOG_WARN("Received message from unknown connection: ", message.connection.id);
                return;
            }
            
            std::shared_ptr<EngineIOSession> session;
            {
                std::lock_guard<std::mutex> lock(m_sessions_mutex);
                auto it = m_sessions.find(session_id);
                if (it != m_sessions.end()) {
                    session = it->second;
                }
            }
            
            if (session) {
                session->process_message(message.payload);
            } else {
                LOG_WARN("Session not found for message: ", session_id);
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error processing transport message: ", e.what());
        }
    }
    
    void on_connection_close(const transport::ConnectionHandle& connection, 
                           int code, const std::string& reason) override {
        try {
            std::string session_id = get_session_id_for_connection(connection.id);
            if (!session_id.empty()) {
                // Mark the transport as closed in the session before cleanup
                {
                    std::lock_guard<std::mutex> lock(m_sessions_mutex);
                    auto session_it = m_sessions.find(session_id);
                    if (session_it != m_sessions.end()) {
                        session_it->second->mark_transport_closed();
                    }
                }
                
                cleanup_session(session_id, reason);
                LOG_DEBUG("Connection closed for session: ", session_id, " - ", reason);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling connection close: ", e.what());
        }
    }
    
    void on_error(const transport::ConnectionHandle& connection, 
                 const std::string& error) override {
        try {
            std::string session_id = get_session_id_for_connection(connection.id);
            if (!session_id.empty()) {
                auto handler = get_handler();
                if (handler) {
                    handler->on_engine_io_session_error(session_id, error);
                }
                LOG_WARN("Transport error for session ", session_id, ": ", error);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling transport error: ", e.what());
        }
    }

    // EngineIOSessionHandler interface
    void on_session_open(const std::string& session_id) override {
        auto handler = get_handler();
        if (handler) {
            handler->on_engine_io_session_open(session_id);
        }
        LOG_DEBUG("Engine.IO session opened: ", session_id);
    }
    
    void on_session_message(const std::string& session_id, const std::string& message) override {
        auto handler = get_handler();
        if (handler) {
            handler->on_engine_io_message(session_id, message);
        }
        LOG_TRACE("Engine.IO message received from session: ", session_id);
    }
    
    void on_session_close(const std::string& session_id, const std::string& reason) override {
        cleanup_session(session_id, reason);
        
        auto handler = get_handler();
        if (handler) {
            handler->on_engine_io_session_close(session_id, reason);
        }
        LOG_DEBUG("Engine.IO session closed: ", session_id, " - ", reason);
    }
    
    void on_session_error(const std::string& session_id, const std::string& error) override {
        auto handler = get_handler();
        if (handler) {
            handler->on_engine_io_session_error(session_id, error);
        }
        LOG_WARN("Engine.IO session error: ", session_id, " - ", error);
    }

private:
    std::shared_ptr<transport::Transport> find_transport_for_connection(
        const transport::ConnectionHandle& connection) {
        std::lock_guard<std::mutex> lock(m_transports_mutex);
        // Prefer the transport that knows about this connection
        for (auto& t : m_transports) {
            if (t && t->get_connection_info(connection)) {
                return t;
            }
        }
        // Fallback: first transport if any
        return m_transports.empty() ? nullptr : m_transports.front();
    }
    
    std::string get_session_id_for_connection(const transport::ConnectionId& connection_id) {
        std::lock_guard<std::mutex> lock(m_sessions_mutex);
        auto it = m_connection_to_session.find(connection_id);
        return (it != m_connection_to_session.end()) ? it->second : std::string();
    }
    
    void cleanup_session(const std::string& session_id, const std::string& reason) {
        std::shared_ptr<EngineIOSession> session_to_close;
        
        {
            std::lock_guard<std::mutex> sessions_lock(m_sessions_mutex);
            std::lock_guard<std::mutex> timers_lock(m_timers_mutex);
            
            // Find and remove session
            auto session_it = m_sessions.find(session_id);
            if (session_it != m_sessions.end()) {
                session_to_close = session_it->second;
                
                // Remove connection mapping
                auto connection_id = session_it->second->get_connection().id;
                m_connection_to_session.erase(connection_id);
                
                // Remove session from map
                m_sessions.erase(session_it);
            }
            
            // Cancel and remove timer
            auto timer_it = m_timers.find(session_id);
            if (timer_it != m_timers.end()) {
                boost::system::error_code ec;
                timer_it->second->cancel(ec);
                m_timers.erase(timer_it);
            }
        }
        
        // Close the session outside the lock to avoid deadlocks
        if (session_to_close) {
            session_to_close->close(reason);
        }
    }
    
    std::shared_ptr<EngineIOServerHandler> get_handler() {
        std::lock_guard<std::mutex> lock(m_handler_mutex);
        return m_handler;
    }

private:
    asio::io_service& m_io_service;
    EngineIOConfig m_config;
    
    mutable std::mutex m_handler_mutex;
    std::shared_ptr<EngineIOServerHandler> m_handler;
    
    mutable std::mutex m_transports_mutex;
    std::vector<std::shared_ptr<transport::Transport>> m_transports;
    
    mutable std::mutex m_sessions_mutex;
    std::unordered_map<std::string, std::shared_ptr<EngineIOSession>> m_sessions;
    std::unordered_map<transport::ConnectionId, std::string> m_connection_to_session;
    
    mutable std::mutex m_timers_mutex;
    std::unordered_map<std::string, std::shared_ptr<asio::steady_timer>> m_timers;
};

} // namespace engineio
} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_ENGINE_IO_SERVER_HPP
