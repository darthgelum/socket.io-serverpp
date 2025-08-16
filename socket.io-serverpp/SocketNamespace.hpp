#ifndef SOCKETIO_SERVERPP_SOCKETNAMESPACE_HPP
#define SOCKETIO_SERVERPP_SOCKETNAMESPACE_HPP

#include "Constants.hpp"
#include "Error.hpp"
#include "Logger.hpp"
#include "Socket.hpp"
#include "config.hpp"
#include <memory>

#include "lib/rapidjson/document.h"

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

class SIOServer;
class Socket;

/**
 * @brief Manages sockets within a specific namespace
 */
class SocketNamespace {
  friend SIOServer;

public:
  /**
   * @brief Constructs a new SocketNamespace
   * @param nsp Namespace name
   * @param server WebSocket server reference
   */
  SocketNamespace(const string &nsp, wsserver &server)
      : m_namespace(nsp), m_wsserver(server) {
    LOG_INFO("SocketNamespace created: ", nsp.empty() ? "/" : nsp);
  }

  /**
   * @brief Sets connection event handler
   * @param cb Callback to execute when a socket connects
   */
  void onConnection(std::function<void(Socket &)> cb) {
    sig_Connection.connect(cb);
    LOG_DEBUG("Connection handler registered for namespace: ", m_namespace);
  }

  /**
   * @brief Sets disconnection event handler
   * @param cb Callback to execute when a socket disconnects
   */
  void onDisconnection(std::function<void(Socket &)> cb) {
    sig_Disconnection.connect(cb);
    LOG_DEBUG("Disconnection handler registered for namespace: ", m_namespace);
  }

  /**
   * @brief Sends data to all sockets in this namespace
   * @param data JSON data to send
   */
  void send(const string &data) {
    if (data.empty()) {
      LOG_WARN("Attempting to send empty data");
      return;
    }
    
    size_t sent_count = 0;
    for (const auto &socket_pair : m_sockets) {
      try {
        socket_pair.second->send(data);
        ++sent_count;
      } catch (const std::exception& e) {
        LOG_WARN("Failed to send to socket: ", e.what());
      }
    }
    LOG_DEBUG("Sent data to ", sent_count, " sockets in namespace: ", m_namespace);
  }

  /**
   * @brief Emits an event to all sockets in this namespace
   * @param name Event name
   * @param data Event data
   */
  void emit(const string &name, const string &data) {
    if (name.empty()) {
      LOG_WARN("Attempting to emit event with empty name");
      return;
    }
    
    size_t sent_count = 0;
    for (const auto &socket_pair : m_sockets) {
      try {
        socket_pair.second->emit(name, data);
        ++sent_count;
      } catch (const std::exception& e) {
        LOG_WARN("Failed to emit to socket: ", e.what());
      }
    }
    LOG_DEBUG("Emitted event '", name, "' to ", sent_count, " sockets in namespace: ", m_namespace);
  }

  /**
   * @brief Gets the namespace name
   * @return Namespace string
   */
  const string& socketNamespace() const { 
    return m_namespace; 
  }

  /**
   * @brief Gets the number of connected sockets
   * @return Socket count
   */
  size_t socket_count() const {
    return m_sockets.size();
  }

  /**
   * @brief Checks if a specific socket is connected to this namespace
   * @param hdl Connection handle
   * @return true if socket exists, false otherwise
   */
  bool has_socket(wspp::connection_hdl hdl) const {
    return m_sockets.find(hdl) != m_sockets.end();
  }

private:
  /**
   * @brief Handles new socket connection
   * @param hdl Connection handle
   */
  void onSocketIoConnection(wspp::connection_hdl hdl) {
    try {
      auto socket = std::make_shared<Socket>(m_wsserver, m_namespace, hdl);
      m_sockets.emplace(hdl, socket);
      
      LOG_DEBUG("Socket connected to namespace: ", m_namespace, 
                " (total: ", m_sockets.size(), ")");
      
      sig_Connection(*socket);
    } catch (const std::exception& e) {
      LOG_ERROR("Error handling socket connection: ", e.what());
    }
  }

  /**
   * @brief Handles socket message
   * @param hdl Connection handle
   * @param msg Message to process
   */
  void onSocketIoMessage(wspp::connection_hdl hdl, const Message &msg) {
    auto iter = m_sockets.find(hdl);
    if (iter != m_sockets.end()) {
      try {
        iter->second->onMessage(msg);
        LOG_TRACE("Processed message for socket in namespace: ", m_namespace);
      } catch (const std::exception& e) {
        LOG_ERROR("Error processing socket message: ", e.what());
      }
    } else {
      LOG_WARN("Message received for unknown socket in namespace: ", m_namespace);
    }
  }

  /**
   * @brief Handles socket event
   * @param hdl Connection handle
   * @param msg Event message
   */
  void onSocketIoEvent(wspp::connection_hdl hdl, const Message &msg) {
    try {
      rapidjson::Document json;
      json.Parse<0>(msg.data.c_str());

      string event_name;
      if (json.IsArray() && json.Size() >= 1 && json[0u].IsString()) {
        event_name = json[0u].GetString();
      } else {
        LOG_WARN("Invalid event format in namespace: ", m_namespace);
        return;
      }

      auto sender_iter = m_sockets.find(hdl);
      if (sender_iter != m_sockets.end()) {
        // Deliver to sender's handlers
        sender_iter->second->onEvent(event_name, json, msg.data);
        LOG_TRACE("Processed event '", event_name, "' from socket in namespace: ", m_namespace);

        // Broadcast to other sockets in namespace using v5 framing
        broadcast_event_to_others(hdl, msg.data);
      } else {
        LOG_WARN("Event received from unknown socket in namespace: ", m_namespace);
      }
    } catch (const std::exception& e) {
      LOG_ERROR("Error processing socket event: ", e.what());
    }
  }

  /**
   * @brief Broadcasts event to all other sockets in namespace
   * @param sender_hdl Handle of the sending socket (excluded from broadcast)
   * @param event_data Event data to broadcast
   */
  void broadcast_event_to_others(wspp::connection_hdl sender_hdl, const string& event_data) {
    size_t broadcast_count = 0;
    
    for (const auto &socket_pair : m_sockets) {
      // Skip sender
      if (!socket_pair.first.owner_before(sender_hdl) &&
          !sender_hdl.owner_before(socket_pair.first)) {
        continue;
      }

      try {
        string payload = build_broadcast_frame(event_data);
        m_wsserver.send(socket_pair.first, payload, wspp::frame::opcode::value::text);
        ++broadcast_count;
      } catch (const std::exception& e) {
        LOG_WARN("Failed to broadcast to socket: ", e.what());
      }
    }
    
    LOG_TRACE("Broadcast event to ", broadcast_count, " sockets in namespace: ", m_namespace);
  }

  /**
   * @brief Builds broadcast frame for event data
   * @param event_data Event data
   * @return Formatted frame string
   */
  string build_broadcast_frame(const string& event_data) const {
    string payload;
    payload += engine_io::MESSAGE; // Engine.IO message
    payload += std::to_string(socket_io::EVENT); // Socket.IO EVENT
    
    if (!m_namespace.empty()) {
      payload += m_namespace + ",";
    }
    
    payload += event_data; // already JSON array
    return payload;
  }

  /**
   * @brief Handles socket disconnection
   * @param hdl Connection handle
   */
  void onSocketIoDisconnect(wspp::connection_hdl hdl) {
    auto iter = m_sockets.find(hdl);
    if (iter != m_sockets.end()) {
      try {
        sig_Disconnection(*(iter->second));
        m_sockets.erase(iter);
        
        LOG_DEBUG("Socket disconnected from namespace: ", m_namespace, 
                  " (remaining: ", m_sockets.size(), ")");
      } catch (const std::exception& e) {
        LOG_ERROR("Error handling socket disconnection: ", e.what());
      }
    }
  }

  // Member variables
  const string m_namespace;
  wsserver &m_wsserver;

  signal<void(Socket &)> sig_Connection;
  signal<void(Socket &)> sig_Disconnection;
  map<wspp::connection_hdl, std::shared_ptr<Socket>,
      std::owner_less<wspp::connection_hdl>> m_sockets;
};

} // namespace lib
using lib::SocketNamespace;
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_SOCKETNAMESPACE_HPP
