
#include "SIOServer.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <cstdio>  // For std::remove

using namespace std;
using namespace socketio_serverpp;

/**
 * @brief Example usage of the Socket.IO server
 * 
 * This example demonstrates:
 * - Creating a server instance
 * - Setting up default namespace handlers
 * - Creating custom namespaces
 * - Handling connection/disconnection events
 * - Processing custom events and messages
 */
int main() {
  try {
    boost::asio::io_service io_service;
    SIOServer io(io_service);
    
    // Configure logging level (optional)
    socketio_serverpp::lib::Logger::instance().set_level(
        socketio_serverpp::lib::LogLevel::DEBUG);
    
    // Clean up any existing socket file to prevent "Address already in use" errors
    const std::string socket_path = "/tmp/dorascgi";
    if (std::remove(socket_path.c_str()) == 0) {
      cout << "Removed existing socket file: " << socket_path << endl;
    }
    
    // Start listening
    io.listen(socket_path, 9001);

    // Set up default namespace handlers
    io.sockets()->onConnection([&](Socket &socket) {
      cout << "Client connected to default namespace (UUID: " 
           << socket.uuid() << ")" << endl;
      
      // Send welcome message
      socket.emit("welcome", "\"Hello from server!\"");

      // Handle chat messages and let them broadcast automatically
      socket.on("chat_message", [&](const Event &event) {
        cout << "Received chat message: " << event.data() << endl;
        // The message is automatically broadcast to other clients by SocketNamespace
      });

      // Handle ping-pong for testing
      socket.on("ping", [&socket](const Event &event) {
        cout << "Received ping, sending pong" << endl;
        socket.emit("pong", "\"Server response\"");
      });

      socket.on("other_event", [](const Event &event) {
        cout << "Other event received: " << event.data() << endl;
      });
    });

    io.sockets()->onDisconnection([](Socket &socket) {
      cout << "Client disconnected from default namespace" << endl;
    });

    // Set up custom chat namespace
    auto chat = io.of("/chat");
    chat->onConnection([&](Socket &socket) {
      cout << "Client connected to /chat namespace" << endl;
      
      // Send private message to this socket
      socket.emit("private_message", "\"Welcome to chat!\"");
      
      // Broadcast to all sockets in /chat namespace
      chat->emit("user_joined", "\"A user joined the chat\"");
      
      // Handle chat-specific events
      socket.on("chat_message", [chat](const Event &event) {
        cout << "Chat message in /chat: " << event.data() << endl;
        // Broadcast to all in chat namespace
        chat->emit("broadcast_message", event.data());
      });
    });

    chat->onDisconnection([chat](Socket &socket) {
      cout << "Client disconnected from /chat namespace" << endl;
      chat->emit("user_left", "\"A user left the chat\"");
    });

    cout << "Socket.IO server started on port 9001" << endl;
    cout << "SCGI socket: /tmp/dorascgi" << endl;
    cout << "Press Ctrl+C to stop" << endl;

    io.run();
    
  } catch (const socketio_serverpp::lib::SocketIOException& e) {
    cerr << "Socket.IO error: " << e.what() << endl;
    return 1;
  } catch (const std::exception& e) {
    cerr << "Error: " << e.what() << endl;
    return 1;
  }
  
  return 0;
}
