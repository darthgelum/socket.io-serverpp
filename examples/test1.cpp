#include <iostream>

#define SOCKETIO_SERVERPP_CPP11_STL_ 1

#include "../socket.io-serverpp/socketio-serverpp.hpp"

using namespace std;

int main() {
  boost::asio::io_service io_service;

  // Create Socket.IO server with new layered architecture using factory method
  auto server = socketio_serverpp::lib::socketio::SocketIOServer::create(io_service);

  server->listen(9003);

  auto chat = server->get_namespace("/chat");
  chat->onConnection([&](socketio_serverpp::Socket& socket) {
    socket.on("my event", [](const socketio_serverpp::Event &event) {
      cout << "received '" << event.name() << "' with " << event.data() << endl;
    });

    cout << "a socket with namespace /chat connected" << endl;

    socket.emit("a message", std::string("only socket will get"));
    chat->emit("a message", std::string("all in /chat will get"));
  });

  io_service.run();
}
