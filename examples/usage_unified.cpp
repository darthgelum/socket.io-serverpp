#include "../socket.io-serverpp/socketio-serverpp.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <string>

using Socket = socketio_serverpp::lib::Socket;
using Event = socketio_serverpp::lib::Event;
using SocketNamespace = socketio_serverpp::lib::SocketNamespace;

int main() {
  try {
    boost::asio::io_service io_service;

    // Create Socket.IO server
    auto server = socketio_serverpp::lib::socketio::SocketIOServer::create(io_service);

    // Optional: increase log verbosity
    socketio_serverpp::lib::Logger::instance().set_level(socketio_serverpp::lib::LogLevel::DEBUG);

    // Unified single-port listen (HTTP polling + WebSocket auto-upgrade)
    int port = 8080;
    server->listen(port);

    // Default namespace handlers
    auto nsp = server->get_namespace("/");
    nsp->onConnection([&](Socket& socket) {
      std::cout << "[unified] Client connected (ID: " << socket.get_id() << ")" << std::endl;
      socket.emit("welcome", std::string("Hello from unified server!"));

      socket.on("chat_message", [&](const Event &event) {
        std::cout << "[unified] chat_message: " << event.data() << std::endl;
        // Broadcast on the same event name so simple clients receive it without extra listeners
        nsp->emit("chat_message", event.data());
      });

      socket.on("ping", [&socket](const Event &event) {
        (void)event;
        socket.emit("pong", std::string("pong from unified"));
      });
    });

    nsp->onDisconnection([](Socket& socket) {
      std::cout << "[unified] Client disconnected" << std::endl;
    });

    std::cout << "Unified Socket.IO server listening on http://localhost:" << port << "/socket.io/" << std::endl;
    std::cout << "Supports Engine.IO long-polling and WebSocket upgrade on the same port" << std::endl;

    io_service.run();
  } catch (const socketio_serverpp::lib::SocketIOException& e) {
    std::cerr << "Socket.IO error: " << e.what() << std::endl;
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
