#include "../socket.io-serverpp/socketio-serverpp.hpp"
#include <boost/asio.hpp>
#include <iostream>

int main() {
    using namespace socketio_serverpp::lib;
    using socketio::SocketIOServer;

    try {
        boost::asio::io_service io;
        auto server = socketio::SocketIOServer::create(io);
        Logger::instance().set_level(LogLevel::DEBUG);

        // Start both transports: WebSocket on 8081 and HTTP polling on 8080
        server->listen(8081, 8080);

        auto def = server->get_namespace("/");
        def->onConnection([&](Socket& s){
            std::cout << "Client connected: " << s.get_id() << std::endl;
            s.emit("welcome", std::string("Hello from polling+ws server"));
        });

        std::cout << "Listening WS: 8081, Polling: 8080" << std::endl;
        std::cout << "Engine.IO polling endpoint: http://localhost:8080/socket.io/?EIO=4&transport=polling" << std::endl;

        io.run();
    } catch (const SocketIOException& e) {
        std::cerr << "Socket.IO error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
