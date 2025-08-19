
#include "../socket.io-serverpp/socketio-serverpp.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <string>

using namespace std;
// Note: Using explicit types instead of namespace alias to avoid confusion
using Socket = socketio_serverpp::lib::Socket;
using Event = socketio_serverpp::lib::Event;
using SocketNamespace = socketio_serverpp::lib::SocketNamespace;

/**
 * @brief Example usage of the Socket.IO server with new layered architecture
 * 
 * This example demonstrates:
 * - Creating a server instance with the new architecture
 * - Setting up default namespace handlers
 * - Creating custom namespaces
 * - Handling connection/disconnection events
 * - Processing custom events and messages
 */
int main() {
  try {
    boost::asio::io_service io_service;
    
    // Create Socket.IO server with the new layered architecture using factory method
    auto server = socketio_serverpp::lib::socketio::SocketIOServer::create(io_service);
    
    // Configure logging level (optional)
    socketio_serverpp::lib::Logger::instance().set_level(socketio_serverpp::lib::LogLevel::DEBUG);
    
    // Start listening on WebSocket port
    server->listen(8081);

    // Set up default namespace handlers
    auto default_ns = server->get_namespace("/");
    default_ns->onConnection([&](Socket& socket) {
      cout << "Client connected to default namespace (ID: " 
           << socket.get_id() << ")" << endl;
      
      // Send welcome message
      socket.emit("welcome", std::string("Hello from server!"));

      // Handle chat messages
      socket.on("chat_message", [&](const Event &event) {
        cout << "Received chat message: " << event.data() << endl;
        // Broadcast to all sockets in namespace
        default_ns->emit("broadcast_message", event.data());
      });

      // Handle ping-pong for testing
      socket.on("ping", [&socket](const Event &event) {
        cout << "Received ping, sending pong" << endl;
        socket.emit("pong", std::string("Server response"));
      });

      socket.on("other_event", [](const Event &event) {
        cout << "Other event received: " << event.data() << endl;
      });
    });

    default_ns->onDisconnection([](Socket& socket) {
      cout << "Client disconnected from default namespace" << endl;
    });

    // Set up custom chat namespace
    auto chat = server->get_namespace("/chat");
    chat->onConnection([&](Socket& socket) {
      cout << "Client connected to /chat namespace" << endl;
      // Send private message to this socket
      socket.emit("private_message", std::string("Welcome to chat!"));
      
      // Broadcast to all sockets in /chat namespace
      chat->emit("user_joined", std::string("A user joined the chat"));
      
      // Handle chat-specific events
      socket.on("chat_message", [&chat](const Event &event) {
        cout << "Chat message in /chat: " << event.data() << endl;
        // Broadcast to all in chat namespace
        chat->emit("broadcast_message", event.data());
      });
    });

    chat->onDisconnection([&chat](Socket& socket) {
      cout << "Client disconnected from /chat namespace" << endl;
      chat->emit("user_left", std::string("A user left the chat"));
    });

    cout << "Socket.IO server started on WebSocket port 8081" << endl;
    cout << "Connect with WebSocket client to ws://localhost:8081/socket.io/" << endl;
    cout << "Press Ctrl+C to stop" << endl;

    io_service.run();
    
  } catch (const socketio_serverpp::lib::SocketIOException& e) {
    cerr << "Socket.IO error: " << e.what() << endl;
    return 1;
  } catch (const std::exception& e) {
    cerr << "Error: " << e.what() << endl;
    return 1;
  }
  
  return 0;
}
