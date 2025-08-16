#include <iostream>

#define SOCKETIO_SERVERPP_CPP11_STL_ 1

#include "SIOServer.hpp"

using namespace std;

int main() {
  boost::asio::io_service io_service;

  socketio_serverpp::SIOServer io(io_service);

  io.listen("0.0.0.0", 9000, 9003);

  // io.sockets.on("connection", [](socketio-serverpp::socket socket)
#if 0
    io.sockets().on("connection", [](socketio-serverpp::socket socket)
    {
        socket.emit('my event', 'some data');
    0   socket.on('other event', [](const string& data)
        {
            cout << data << endl;
        });
    });
#endif

  auto chat = io.of("/chat");
  chat->onConnection([&](socketio_serverpp::Socket &socket) {
    socket.on("my event", [](const socketio_serverpp::Event &event) {
      cout << "reicevd '" << event.name() << "' with " << event.data() << endl;
    });

    cout << "a socket with namespace /chat connected" << endl;

    socket.emit("a message", "only socket will get");
    chat->emit("a message", "all in /chat will get");
  });

  io_service.run();
}
