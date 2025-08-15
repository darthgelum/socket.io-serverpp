
#include <boost/asio.hpp>
#include "Server.hpp"
#include <iostream>
#include <string>

using namespace std;
using namespace socketio_serverpp;

/* This example only a draft of the interface that
 * should be implemented.
 */


int main()
{
    boost::asio::io_service io_service;
    Server io(io_service);
    io.listen("/tmp/dorascgi", 9000);


    io.sockets()->onConnection([&](Socket& socket)
    {
        cout << "Client connected to default namespace" << endl;
        socket.emit("my event", "\"some data\"");
        
        // Handle chat messages and broadcast them
        socket.on("chat_message", [&](const Event& event)
        {
            cout << "Received chat message: " << event.data() << endl;
            // The message is automatically broadcast to other clients by SocketNamespace
        });
        
        socket.on("other event", [](const Event& event)
        {
            cout << "Other event: " << event.data() << endl;
        });
    });

    auto chat = io.of("/chat");
    chat->onConnection([&](Socket& socket)
    {
        socket.emit("a message", "only socket will get");
        chat->emit("a message", "all in /chat will get");
    });

    io.run();
}
