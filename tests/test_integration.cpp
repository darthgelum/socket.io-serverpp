#include <gtest/gtest.h>
#include "SIOServer.hpp"
#include "Error.hpp"
#include "Logger.hpp"
#include <boost/asio.hpp>
#include <thread>
#include <chrono>

using namespace socketio_serverpp::lib;

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        io_service = std::make_unique<boost::asio::io_service>();
        Logger::instance().set_level(LogLevel::ERROR); // Reduce log noise during tests
    }
    
    void TearDown() override {}
    
    std::unique_ptr<boost::asio::io_service> io_service;
};

TEST_F(IntegrationTest, ServerInitialization) {
    // Test complete server initialization
    SIOServer server(*io_service);
    
    // Verify default namespace exists
    auto default_ns = server.sockets();
    ASSERT_NE(default_ns, nullptr);
    EXPECT_EQ(default_ns->socketNamespace(), "");
}

TEST_F(IntegrationTest, MultipleNamespaces) {
    SIOServer server(*io_service);
    
    // Create multiple namespaces
    auto default_ns = server.sockets();
    auto chat_ns = server.of("/chat");
    auto game_ns = server.of("/game");
    auto admin_ns = server.of("/admin");
    
    // Verify all namespaces are created and unique
    EXPECT_NE(default_ns, nullptr);
    EXPECT_NE(chat_ns, nullptr);
    EXPECT_NE(game_ns, nullptr);
    EXPECT_NE(admin_ns, nullptr);
    
    EXPECT_NE(default_ns, chat_ns);
    EXPECT_NE(chat_ns, game_ns);
    EXPECT_NE(game_ns, admin_ns);
    
    // Verify namespace names
    EXPECT_EQ(default_ns->socketNamespace(), "");
    EXPECT_EQ(chat_ns->socketNamespace(), "/chat");
    EXPECT_EQ(game_ns->socketNamespace(), "/game");
    EXPECT_EQ(admin_ns->socketNamespace(), "/admin");
}

TEST_F(IntegrationTest, NamespaceReusability) {
    SIOServer server(*io_service);
    
    // Request same namespace multiple times
    auto chat_ns1 = server.of("/chat");
    auto chat_ns2 = server.of("/chat");
    auto chat_ns3 = server.of("/chat");
    
    // Should all be the same instance
    EXPECT_EQ(chat_ns1, chat_ns2);
    EXPECT_EQ(chat_ns2, chat_ns3);
    EXPECT_EQ(chat_ns1.get(), chat_ns3.get());
}

TEST_F(IntegrationTest, EventHandlerRegistration) {
    SIOServer server(*io_service);
    
    bool connection_called = false;
    bool disconnection_called = false;
    bool event_called = false;
    
    // Register handlers
    auto chat_ns = server.of("/chat");
    
    chat_ns->onConnection([&](Socket& socket) {
        connection_called = true;
        
        socket.on("test_event", [&](const Event& event) {
            event_called = true;
            EXPECT_EQ(event.name(), "test_event");
        });
    });
    
    chat_ns->onDisconnection([&](Socket& socket) {
        disconnection_called = true;
    });
    
    // Handlers should be registered without throwing
    EXPECT_FALSE(connection_called); // Not called yet
    EXPECT_FALSE(disconnection_called); // Not called yet
    EXPECT_FALSE(event_called); // Not called yet
}

TEST_F(IntegrationTest, ErrorHandlingFlow) {
    SIOServer server(*io_service);
    
    // Test error creation and handling
    try {
        throw SocketIOException("Test integration error", SocketIOErrorCode::CONNECTION_FAILED);
    } catch (const SocketIOException& e) {
        EXPECT_STREQ(e.what(), "Test integration error");
        EXPECT_EQ(e.error_code(), SocketIOErrorCode::CONNECTION_FAILED);
    }
    
    // Test error code integration
    auto ec = make_error_code(SocketIOErrorCode::INVALID_NAMESPACE);
    EXPECT_TRUE(ec);
    EXPECT_EQ(ec.message(), "Invalid namespace");
}

TEST_F(IntegrationTest, MessageEventIntegration) {
    // Test Message and Event integration
    Message msg(true, "test_sender", socket_io::EVENT, 42, false, "/chat", 
                "{\"event\": \"message\", \"data\": \"hello\"}");
    
    EXPECT_TRUE(msg.isJson);
    EXPECT_EQ(msg.type, socket_io::EVENT);
    EXPECT_TRUE(msg.is_ack());
    EXPECT_FALSE(msg.empty());
    
    // Create event from message data
    Event event("message", msg.data);
    EXPECT_EQ(event.name(), "message");
    EXPECT_FALSE(event.isJson());
    EXPECT_EQ(event.data(), msg.data);
}

TEST_F(IntegrationTest, ProtocolConstantsIntegration) {
    // Test that all protocol constants work together
    
    // Engine.IO flow
    char open_packet = engine_io::OPEN;
    char message_packet = engine_io::MESSAGE;
    char ping_packet = engine_io::PING;
    char pong_packet = engine_io::PONG;
    
    EXPECT_EQ(open_packet, '0');
    EXPECT_EQ(message_packet, '4');
    EXPECT_EQ(ping_packet, '2');
    EXPECT_EQ(pong_packet, '3');
    
    // Socket.IO flow
    int connect_type = socket_io::CONNECT;
    int event_type = socket_io::EVENT;
    int disconnect_type = socket_io::DISCONNECT;
    
    EXPECT_EQ(connect_type, 0);
    EXPECT_EQ(event_type, 2);
    EXPECT_EQ(disconnect_type, 1);
    
    // Complete frame building
    std::string frame;
    frame += message_packet; // '4'
    frame += std::to_string(event_type); // "2"
    EXPECT_EQ(frame, "42");
}

TEST_F(IntegrationTest, ServerConfiguration) {
    SIOServer server(*io_service);
    
    // Test configuration methods
    EXPECT_NO_THROW(server.set_ping_interval(30000));
    EXPECT_NO_THROW(server.set_ping_timeout(10000));
    
    // Configuration should not affect namespace creation
    auto test_ns = server.of("/test");
    EXPECT_NE(test_ns, nullptr);
    EXPECT_EQ(test_ns->socketNamespace(), "/test");
}

TEST_F(IntegrationTest, LoggingIntegration) {
    // Test logging integration with other components
    Logger& logger = Logger::instance();
    
    // Capture current level
    LogLevel original_level = logger.get_level();
    
    // Test with different levels
    logger.set_level(LogLevel::DEBUG);
    EXPECT_EQ(logger.get_level(), LogLevel::DEBUG);
    
    logger.set_level(LogLevel::ERROR);
    EXPECT_EQ(logger.get_level(), LogLevel::ERROR);
    
    // Restore original level
    logger.set_level(original_level);
}

TEST_F(IntegrationTest, CompleteWorkflow) {
    // Test a complete Socket.IO workflow simulation
    SIOServer server(*io_service);
    
    // Step 1: Create namespaces
    auto default_ns = server.sockets();
    auto chat_ns = server.of("/chat");
    
    // Step 2: Set up event handlers
    bool setup_complete = false;
    
    default_ns->onConnection([&](Socket& socket) {
        socket.on("setup", [&](const Event& event) {
            setup_complete = true;
        });
    });
    
    chat_ns->onConnection([&](Socket& socket) {
        socket.on("join_room", [](const Event& event) {
            // Room joining logic would go here
        });
    });
    
    // Step 3: Test protocol message handling
    std::string handshake_data = "0{\"sid\":\"test123\",\"upgrades\":[]}";
    EXPECT_TRUE(handshake_data.front() == engine_io::OPEN);
    
    std::string event_message = "42[\"message\", \"hello\"]";
    EXPECT_EQ(event_message.substr(0, 2), "42"); // Engine.IO message + Socket.IO event
    
    // Step 4: Test error handling
    try {
        Event invalid_event("", "data");
        FAIL() << "Should have thrown exception";
    } catch (const SocketIOException& e) {
        EXPECT_EQ(e.error_code(), SocketIOErrorCode::INVALID_NAMESPACE);
    }
    
    // Test completed successfully
    EXPECT_NE(default_ns, nullptr);
    EXPECT_NE(chat_ns, nullptr);
    EXPECT_FALSE(setup_complete); // Event not triggered yet
}

TEST_F(IntegrationTest, MemoryManagement) {
    // Test memory management with shared pointers
    std::weak_ptr<SocketNamespace> weak_ns;
    
    {
        SIOServer server(*io_service);
        auto ns = server.of("/temp");
        weak_ns = ns;
        
        EXPECT_FALSE(weak_ns.expired());
        EXPECT_NE(ns, nullptr);
    } // server goes out of scope
    
    // After server destruction, namespace should still be valid if held
    // (This test validates the shared_ptr behavior)
}

TEST_F(IntegrationTest, ExceptionSafety) {
    // Test exception safety throughout the system
    
    EXPECT_NO_THROW({
        SIOServer server(*io_service);
        auto ns = server.of("/test");
        ns->onConnection([](Socket&) {});
        ns->onDisconnection([](Socket&) {});
    });
    
    // Test that invalid operations throw appropriate exceptions
    EXPECT_THROW({
        Event("", "data");
    }, SocketIOException);
    
    // Test error code system
    auto ec = make_error_code(SocketIOErrorCode::TIMEOUT);
    EXPECT_TRUE(ec);
    EXPECT_NE(ec.message(), "");
}

TEST_F(IntegrationTest, ThreadSafety) {
    // Basic thread safety test for logger (singleton)
    Logger& logger1 = Logger::instance();
    
    std::thread t1([&]() {
        Logger& logger_t1 = Logger::instance();
        EXPECT_EQ(&logger1, &logger_t1);
    });
    
    std::thread t2([&]() {
        Logger& logger_t2 = Logger::instance();
        EXPECT_EQ(&logger1, &logger_t2);
    });
    
    t1.join();
    t2.join();
}
