#ifndef SOCKETIO_SERVERPP_SIOSERVER_HPP
#define SOCKETIO_SERVERPP_SIOSERVER_HPP

#include "Constants.hpp"
#include "Error.hpp"
#include "Logger.hpp"
#include "Message.hpp"
#include "SocketNamespace.hpp"
#include "config.hpp"
#include "HttpServer.hpp"
#include "uuid.hpp"
#include <functional>
#include <memory>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <boost/regex.hpp>
#include <cctype>
#include <chrono>
#include <map>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

class SocketNamespace;

using std::map;
using std::string;
using std::vector;

// Import HTTP server classes
using HttpServer = lib::HttpServer;
using HttpRequest = lib::HttpRequest;
using HttpResponse = lib::HttpResponse;

class SIOServer {
public:
  /**
   * @brief Constructs a new SIOServer instance
   * @param io_service Boost ASIO IO service for handling async operations
   */
  explicit SIOServer(asio::io_service &io_service)
      : m_io_service(io_service),
        m_reSockIoMsg("^(\\d):([\\d+]*):([^:]*):?(.*)", boost::regex::perl),
        m_heartBeat(timeouts::DEFAULT_HEARTBEAT),
        m_closeTime(timeouts::DEFAULT_CLOSE_TIME),
        m_pingInterval(timeouts::DEFAULT_PING_INTERVAL),
        m_pingTimeout(timeouts::DEFAULT_PING_TIMEOUT),
        m_maxPayload(timeouts::DEFAULT_MAX_PAYLOAD),
        m_httpserver(std::make_shared<HttpServer>(io_service)) {
    
    initialize_websocket_server();
    initialize_http_server();
    
    m_sockets = this->of("");
    m_protocols = {"websocket"};
    
    LOG_INFO("SIOServer initialized with ping interval: ", m_pingInterval, "ms");
  }

  /**
   * @brief Starts listening on the specified HTTP address/port and WebSocket port
   * @param http_address IP address for HTTP server (e.g., "0.0.0.0")
   * @param http_port Port number for HTTP connections
   * @param websocket_port Port number for WebSocket connections
   */
  void listen(const string &http_address, int http_port, int websocket_port) {
    try {
      m_httpserver->listen(http_address, static_cast<unsigned short>(http_port));
      m_httpserver->start_accept();

      m_wsserver.listen(websocket_port);
      m_wsserver.start_accept();
      
      LOG_INFO("Server listening on HTTP ", http_address, ":", http_port, 
               " and WebSocket port: ", websocket_port);
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to start listening: ", e.what());
      throw SocketIOException("Failed to start server", SocketIOErrorCode::CONNECTION_FAILED);
    }
  }

  /**
   * @brief Creates or retrieves a namespace
   * @param nsp Namespace name
   * @return Shared pointer to the SocketNamespace
   */
  std::shared_ptr<SocketNamespace> of(const string &nsp) {
    auto iter = m_socket_namespace.find(nsp);
    if (iter == m_socket_namespace.end()) {
      auto snsp = std::make_shared<SocketNamespace>(nsp, m_wsserver);
      m_socket_namespace.emplace(nsp, snsp);
      LOG_DEBUG("Created new namespace: ", nsp);
      return snsp;
    }
    return iter->second;
  }

  /**
   * @brief Gets the default namespace (empty string)
   * @return Shared pointer to the default SocketNamespace
   */
  std::shared_ptr<SocketNamespace> sockets() const { 
    return m_sockets; 
  }

  /**
   * @brief Runs the IO service event loop
   */
  void run() { 
    LOG_INFO("Starting server event loop");
    m_io_service.run(); 
  }

  /**
   * @brief Sets the ping interval for heartbeat
   * @param interval_ms Interval in milliseconds
   */
  void set_ping_interval(int interval_ms) {
    m_pingInterval = interval_ms;
    LOG_DEBUG("Ping interval set to: ", interval_ms, "ms");
  }

  /**
   * @brief Sets the ping timeout for heartbeat
   * @param timeout_ms Timeout in milliseconds
   */
  void set_ping_timeout(int timeout_ms) {
    m_pingTimeout = timeout_ms;
    LOG_DEBUG("Ping timeout set to: ", timeout_ms, "ms");
  }

private:
  /**
   * @brief Initializes the WebSocket server with proper handlers
   */
  void initialize_websocket_server() {
    m_wsserver.init_asio(&m_io_service);
    m_wsserver.set_access_channels(websocketpp::log::alevel::none);
    m_wsserver.set_error_channels(websocketpp::log::elevel::warn);
    m_wsserver.set_message_handler(std::bind(&SIOServer::onWebsocketMessage,
                                             this, std::placeholders::_1,
                                             std::placeholders::_2));
    m_wsserver.set_open_handler(
        std::bind(&SIOServer::onWebsocketOpen, this, std::placeholders::_1));
    m_wsserver.set_close_handler(
        std::bind(&SIOServer::onWebsocketClose, this, std::placeholders::_1));
  }

  /**
   * @brief Initializes the HTTP server with proper handlers
   */
  void initialize_http_server() {
    m_httpserver->set_request_handler(
        std::bind(&SIOServer::onHttpRequest, this, 
                  std::placeholders::_1, std::placeholders::_2));
  }

  // Extract a query parameter from a resource string like
  // "/socket.io/?EIO=4&transport=websocket&sid=ABC"
  static string get_query_param(const string &resource, const string &key) {
    auto qpos = resource.find('?');
    if (qpos == string::npos)
      return string();
    auto query = resource.substr(qpos + 1);
    size_t pos = 0;
    while (pos < query.size()) {
      auto amp = query.find('&', pos);
      auto part =
          query.substr(pos, amp == string::npos ? string::npos : amp - pos);
      auto eq = part.find('=');
      string k = (eq == string::npos) ? part : part.substr(0, eq);
      if (k == key) {
        return (eq == string::npos) ? string() : part.substr(eq + 1);
      }
      if (amp == string::npos)
        break;
      pos = amp + 1;
    }
    return string();
  }

  void onHttpRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
      string uri = req.uri();
      string method = req.method();
      string uuid = lib::uuid::uuid1();
      
      LOG_DEBUG("HTTP request: ", method, " ", uri);

      // Handle CORS preflight requests
      if (method == "OPTIONS") {
        resp.set_status(beast::http::status::ok);
        resp.set_header("Access-Control-Allow-Origin", "*");
        resp.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        resp.set_header("Access-Control-Allow-Headers", "Content-Type");
        resp.set_body("");
        return;
      }

      // Handle Socket.IO handshake requests
      if (uri.find("/socket.io/") == 0) {
        // Extract EIO version and transport from query parameters
        string eio_version = get_query_param(uri, "EIO");
        string transport = get_query_param(uri, "transport");
        
        LOG_DEBUG("Socket.IO request - EIO: ", eio_version, ", transport: ", transport);
        
        // For Engine.IO v4, return handshake response
        if (eio_version == "4") {
          std::ostringstream os;
          os << "{\"sid\":\"" << uuid 
             << "\",\"upgrades\":[\"websocket\"]"
             << ",\"pingInterval\":" << m_pingInterval
             << ",\"pingTimeout\":" << m_pingTimeout
             << ",\"maxPayload\":" << m_maxPayload << "}";
          
          resp.set_status(beast::http::status::ok);
          resp.set_content_type("application/json");
          resp.set_header("Access-Control-Allow-Origin", "*");
          resp.set_body(os.str());
          return;
        }
        
        // Legacy Socket.IO v1 handshake (for backwards compatibility)
        if (uri.find("/socket.io/1/") == 0) {
          std::ostringstream os;
          os << uuid + ":";
          if (m_heartBeat > 0) {
            os << m_heartBeat;
          }
          os << ":" << m_closeTime << ":";
          for (const auto &p : m_protocols) {
            os << p << ",";
          }
          
          resp.set_status(beast::http::status::ok);
          resp.set_content_type("text/plain");
          resp.set_header("Access-Control-Allow-Origin", "*");
          resp.set_body(os.str());
          return;
        }
      }
      
      // Default 404 response
      LOG_WARN("Unhandled HTTP request: ", method, " ", uri);
      resp.set_status(beast::http::status::not_found);
      resp.set_body("Not Found");
      
    } catch (const std::exception& e) {
      LOG_ERROR("Error handling HTTP request: ", e.what());
      resp.set_status(beast::http::status::internal_server_error);
      resp.set_body("Internal Server Error");
    }
  }

  void onWebsocketOpen(wspp::connection_hdl hdl) {
    try {
      auto connection = m_wsserver.get_con_from_hdl(hdl);
      string resource = connection->get_resource();
      string uuid;

      // Prefer sid from query string if provided (e.g. reconnect), otherwise
      // generate a new one
      uuid = get_query_param(resource, "sid");
      if (uuid.empty()) {
        uuid = lib::uuid::uuid1();
      }

      // Engine.IO v4: upon WebSocket open, send OPEN packet (type '0') with
      // handshake data We run in WebSocket-only mode, so upgrades is empty
      m_conn_sid[hdl] = uuid; // store per-connection SID
      // initialize heartbeat tracking and schedule first ping
      m_last_pong[hdl] = std::chrono::steady_clock::now();
      
      std::ostringstream os;
      os << engine_io::OPEN;
      os << "{\"sid\":\"" << uuid
         << "\",\"upgrades\":[],\"pingInterval\":" << m_pingInterval
         << ",\"pingTimeout\":" << m_pingTimeout
         << ",\"maxPayload\":" << m_maxPayload << "}";
      
      m_wsserver.send(hdl, os.str(), wspp::frame::opcode::value::text);

      schedule_ping(hdl);
      
      LOG_DEBUG("WebSocket connection opened with SID: ", uuid);
    } catch (const std::exception& e) {
      LOG_ERROR("Error handling WebSocket open: ", e.what());
      try {
        m_wsserver.close(hdl, websocketpp::close::status::abnormal_close, "Server error");
      } catch (...) {
        LOG_ERROR("Failed to close connection after error");
      }
    }
  }

  void onWebsocketMessage(wspp::connection_hdl hdl, wsserver::message_ptr msg) {
    try {
      string payload = msg->get_payload();
      if (payload.empty()) {
        LOG_WARN("Received empty WebSocket message");
        return;
      }

      char eio_type = payload[0];
      LOG_TRACE("Received Engine.IO message type: ", eio_type);
      
      // Engine.IO v4 handling
      if (!handle_engine_io_message(hdl, eio_type, payload)) {
        return; // Message was handled by Engine.IO layer
      }

      // Socket.IO v5 framing inside Engine.IO message
      handle_socket_io_message(hdl, payload.substr(1));
      
    } catch (const std::exception& e) {
      LOG_ERROR("Error handling WebSocket message: ", e.what());
    }
  }

private:
  /**
   * @brief Handles Engine.IO level messages
   * @param hdl Connection handle
   * @param eio_type Engine.IO message type
   * @param payload Full message payload
   * @return true if message should be passed to Socket.IO layer, false if handled
   */
  bool handle_engine_io_message(wspp::connection_hdl hdl, char eio_type, const string& payload) {
    switch (eio_type) {
    case engine_io::PING: // ping (may be '2' or '2probe')
      if (payload == protocol::PROBE_PING) {
        m_wsserver.send(hdl, protocol::PROBE_PONG,
                        wspp::frame::opcode::value::text); // pong probe
      } else {
        // If client pings (not typical in v4), answer with pong and refresh liveness
        m_wsserver.send(hdl, protocol::ENGINE_IO_PONG, 
                        wspp::frame::opcode::value::text);
        m_last_pong[hdl] = std::chrono::steady_clock::now();
      }
      return false;
      
    case engine_io::PONG: // pong from client
      m_last_pong[hdl] = std::chrono::steady_clock::now();
      LOG_TRACE("Received pong from client");
      return false;
      
    case engine_io::CLOSE: // close
      onWebsocketClose(hdl);
      return false;
      
    case engine_io::MESSAGE: // message (Socket.IO payload)
      return true; // Pass to Socket.IO layer
      
    default:
      LOG_WARN("Unsupported Engine.IO packet type: ", eio_type);
      return false;
    }
  }

  /**
   * @brief Handles Socket.IO level messages
   * @param hdl Connection handle
   * @param sio_payload Socket.IO payload (without Engine.IO wrapper)
   */
  void handle_socket_io_message(wspp::connection_hdl hdl, const string& sio_payload) {
    if (sio_payload.empty()) {
      LOG_WARN("Empty Socket.IO payload");
      return;
    }

    int pkt_type = sio_payload[0] - '0';
    if (pkt_type < 0 || pkt_type > 6) {
      LOG_WARN("Invalid Socket.IO packet type: ", pkt_type);
      return;
    }

    size_t idx = 1;
    string nsp;
    
    // Parse optional namespace (starts with '/')
    if (idx < sio_payload.size() && sio_payload[idx] == '/') {
      size_t comma = sio_payload.find(',', idx);
      if (comma != string::npos) {
        nsp = sio_payload.substr(idx, comma - idx);
        idx = comma + 1;
      } else {
        nsp = sio_payload.substr(idx);
        idx = sio_payload.size();
      }
    }

    // Skip optional ack id (digits)
    while (idx < sio_payload.size() && 
           isdigit(static_cast<unsigned char>(sio_payload[idx]))) {
      ++idx;
    }

    string data = (idx < sio_payload.size()) ? sio_payload.substr(idx) : string();

    auto socket_namespace = find_or_create_namespace(nsp);
    if (!socket_namespace) {
      send_namespace_error(hdl, nsp);
      return;
    }

    handle_socket_io_packet(hdl, pkt_type, nsp, data, socket_namespace);
  }

  /**
   * @brief Finds existing namespace or creates default namespace if empty
   * @param nsp Namespace name
   * @return Shared pointer to namespace or nullptr if not found
   */
  std::shared_ptr<SocketNamespace> find_or_create_namespace(const string& nsp) {
    auto socket_namespace = m_socket_namespace.find(nsp);
    if (socket_namespace == m_socket_namespace.end()) {
      // create on demand for default namespace
      if (nsp.empty()) {
        socket_namespace = m_socket_namespace.find("");
        if (socket_namespace != m_socket_namespace.end()) {
          return socket_namespace->second;
        }
      } else {
        LOG_WARN("Namespace not found: ", nsp, " - Available namespaces:");
        for (const auto& ns : m_socket_namespace) {
          LOG_WARN("  - '", ns.first, "'");
        }
      }
      return nullptr;
    }
    return socket_namespace->second;
  }

  /**
   * @brief Sends namespace not found error to client
   * @param hdl Connection handle
   * @param nsp Namespace name
   */
  void send_namespace_error(wspp::connection_hdl hdl, const string& nsp) {
    std::ostringstream resp;
    resp << engine_io::MESSAGE << socket_io::CONNECT_ERROR;
    if (!nsp.empty()) {
      resp << nsp << ",";
    }
    
    // Create detailed error message
    std::ostringstream error_msg;
    error_msg << "{\"message\":\"Namespace '" << nsp << "' not found\",\"code\":\"NAMESPACE_NOT_FOUND\"";
    
    // Add available namespaces for debugging
    if (!m_socket_namespace.empty()) {
      error_msg << ",\"available\":[";
      bool first = true;
      for (const auto& ns : m_socket_namespace) {
        if (!first) error_msg << ",";
        error_msg << "\"" << (ns.first.empty() ? "/" : ns.first) << "\"";
        first = false;
      }
      error_msg << "]";
    }
    error_msg << "}";
    
    resp << error_msg.str();
    
    try {
      m_wsserver.send(hdl, resp.str(), wspp::frame::opcode::value::text);
      LOG_DEBUG("Sent namespace error for: ", nsp);
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to send namespace error: ", e.what());
    }
  }

  /**
   * @brief Handles specific Socket.IO packet types
   * @param hdl Connection handle
   * @param pkt_type Socket.IO packet type
   * @param nsp Namespace
   * @param data Packet data
   * @param socket_namespace Namespace handler
   */
  void handle_socket_io_packet(wspp::connection_hdl hdl, int pkt_type, 
                               const string& nsp, const string& data,
                               std::shared_ptr<SocketNamespace> socket_namespace) {
    switch (pkt_type) {
    case socket_io::CONNECT: {
      socket_namespace->onSocketIoConnection(hdl);
      send_connect_response(hdl, nsp);
      break;
    }
    case socket_io::DISCONNECT: {
      socket_namespace->onSocketIoDisconnect(hdl);
      LOG_DEBUG("Socket disconnected from namespace: ", nsp);
      break;
    }
    case socket_io::EVENT: {
      Message message = {false, "", socket_io::EVENT, 0, false, nsp, data};
      socket_namespace->onSocketIoEvent(hdl, message);
      LOG_TRACE("Handled EVENT for namespace: ", nsp);
      break;
    }
    case socket_io::ACK: // not implemented
    case socket_io::CONNECT_ERROR: // server shouldn't receive
    case socket_io::BINARY_EVENT: // not implemented
    case socket_io::BINARY_ACK: // not implemented
    default:
      LOG_DEBUG("Unhandled Socket.IO packet type: ", pkt_type);
      break;
    }
  }

  /**
   * @brief Sends successful connection response to client
   * @param hdl Connection handle
   * @param nsp Namespace
   */
  void send_connect_response(wspp::connection_hdl hdl, const string& nsp) {
    try {
      string sid;
      auto it = m_conn_sid.find(hdl);
      sid = (it != m_conn_sid.end()) ? it->second : lib::uuid::uuid1();
      
      std::ostringstream resp;
      resp << engine_io::MESSAGE << socket_io::CONNECT;
      if (!nsp.empty()) {
        resp << nsp << ",";
      }
      resp << "{\"sid\":\"" << sid << "\"}";
      
      m_wsserver.send(hdl, resp.str(), wspp::frame::opcode::value::text);
      LOG_DEBUG("Sent connect response for namespace: ", nsp, " with SID: ", sid);
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to send connect response: ", e.what());
    }
  }

  void onWebsocketClose(wspp::connection_hdl hdl) {
    try {
      LOG_DEBUG("WebSocket connection closing");
      
      // Notify all namespaces about disconnection
      for (const auto& sns : m_socket_namespace) {
        sns.second->onSocketIoDisconnect(hdl);
      }

      // Cleanup heartbeat resources
      cleanup_connection_resources(hdl);
      
      LOG_DEBUG("WebSocket connection closed and resources cleaned up");
    } catch (const std::exception& e) {
      LOG_ERROR("Error during WebSocket close: ", e.what());
    }
  }

  /**
   * @brief Cleans up all resources associated with a connection
   * @param hdl Connection handle
   */
  void cleanup_connection_resources(wspp::connection_hdl hdl) {
    // Cancel and remove ping timer
    auto timer_it = m_ping_timers.find(hdl);
    if (timer_it != m_ping_timers.end()) {
      if (timer_it->second) {
        boost::system::error_code ec;
        timer_it->second->cancel(ec);
        if (ec) {
          LOG_WARN("Error canceling ping timer: ", ec.message());
        }
      }
      m_ping_timers.erase(timer_it);
    }
    
    // Remove connection tracking data
    m_last_pong.erase(hdl);
    m_conn_sid.erase(hdl);
  }

  /**
   * @brief Schedules the next ping for a connection
   * @param hdl Connection handle
   */
  void schedule_ping(wspp::connection_hdl hdl) {
    try {
      // Reuse a single timer per connection
      auto &timer_ref = m_ping_timers[hdl];
      if (!timer_ref) {
        timer_ref = std::make_shared<asio::steady_timer>(m_io_service);
      }
      
      auto timer = timer_ref;
      timer->expires_after(std::chrono::milliseconds(m_pingInterval));
      
      timer->async_wait([this, hdl](const boost::system::error_code &ec) {
        if (ec) {
          if (ec != boost::asio::error::operation_aborted) {
            LOG_WARN("Ping timer error: ", ec.message());
          }
          return; // Timer was canceled or other error
        }
        
        handle_ping_timeout(hdl);
      });
    } catch (const std::exception& e) {
      LOG_ERROR("Error scheduling ping: ", e.what());
    }
  }

  /**
   * @brief Handles ping timeout logic
   * @param hdl Connection handle
   */
  void handle_ping_timeout(wspp::connection_hdl hdl) {
    try {
      // Check timeout based on last pong
      auto pong_it = m_last_pong.find(hdl);
      if (pong_it != m_last_pong.end()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - pong_it->second).count();
        
        if (elapsed > (m_pingInterval + m_pingTimeout)) {
          LOG_INFO("Connection ping timeout, closing connection");
          try {
            m_wsserver.close(hdl, websocketpp::close::status::normal, "ping timeout");
          } catch (const std::exception& e) {
            LOG_WARN("Error closing connection on timeout: ", e.what());
          }
          return;
        }
      }
      
      // Send ping
      try {
        m_wsserver.send(hdl, protocol::ENGINE_IO_PING, 
                        wspp::frame::opcode::value::text);
        LOG_TRACE("Sent ping to client");
      } catch (const std::exception& e) {
        LOG_WARN("Error sending ping: ", e.what());
        return;
      }
      
      // Schedule next ping
      schedule_ping(hdl);
    } catch (const std::exception& e) {
      LOG_ERROR("Error in ping timeout handler: ", e.what());
    }
  }
  // Core services
  asio::io_service &m_io_service;
  wsserver m_wsserver;
  std::shared_ptr<HttpServer> m_httpserver;
  
  // Namespace management
  std::shared_ptr<SocketNamespace> m_sockets;
  map<string, std::shared_ptr<SocketNamespace>> m_socket_namespace;
  
  // Protocol parsing
  const boost::regex m_reSockIoMsg;
  
  // Configuration
  int m_heartBeat;
  int m_closeTime;
  vector<string> m_protocols;
  
  // Engine.IO v4 settings (WebSocket-only)
  int m_pingInterval;
  int m_pingTimeout;
  int m_maxPayload;
  
  // Per-connection tracking
  map<wspp::connection_hdl, string, std::owner_less<wspp::connection_hdl>> m_conn_sid;
  map<wspp::connection_hdl, std::shared_ptr<asio::steady_timer>,
      std::owner_less<wspp::connection_hdl>> m_ping_timers;
  map<wspp::connection_hdl, std::chrono::steady_clock::time_point,
      std::owner_less<wspp::connection_hdl>> m_last_pong;
};

} // namespace lib
using lib::SIOServer;
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_SIOSERVER_HPP
