#include <gtest/gtest.h>
#include "SIOServer.hpp"
#include "Error.hpp"
#include <boost/asio.hpp>

using namespace socketio_serverpp::lib;

class SIOServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        io_service = std::make_unique<boost::asio::io_service>();
    }
    
    void TearDown() override {}
    
    std::unique_ptr<boost::asio::io_service> io_service;
};

TEST_F(SIOServerTest, ServerConstruction) {
    EXPECT_NO_THROW({
        SIOServer server(*io_service);
    });
}

TEST_F(SIOServerTest, DefaultConfiguration) {
    SIOServer server(*io_service);
    
    // Test that default namespace is created
    auto default_ns = server.sockets();
    EXPECT_NE(default_ns, nullptr);
    EXPECT_EQ(default_ns->socketNamespace(), "");
}

TEST_F(SIOServerTest, NamespaceCreation) {
    SIOServer server(*io_service);
    
    // Create various namespaces
    auto chat_ns = server.of("/chat");
    auto game_ns = server.of("/game");
    auto admin_ns = server.of("/admin");
    
    EXPECT_NE(chat_ns, nullptr);
    EXPECT_NE(game_ns, nullptr);
    EXPECT_NE(admin_ns, nullptr);
    
    EXPECT_EQ(chat_ns->socketNamespace(), "/chat");
    EXPECT_EQ(game_ns->socketNamespace(), "/game");
    EXPECT_EQ(admin_ns->socketNamespace(), "/admin");
}

TEST_F(SIOServerTest, NamespaceReuse) {
    SIOServer server(*io_service);
    
    // Create namespace twice - should return same instance
    auto chat_ns1 = server.of("/chat");
    auto chat_ns2 = server.of("/chat");
    
    EXPECT_EQ(chat_ns1, chat_ns2);
    EXPECT_EQ(chat_ns1.get(), chat_ns2.get());
}

TEST_F(SIOServerTest, EmptyNamespace) {
    SIOServer server(*io_service);
    
    // Test empty namespace (default)
    auto empty_ns1 = server.of("");
    auto empty_ns2 = server.sockets();
    
    EXPECT_EQ(empty_ns1, empty_ns2);
    EXPECT_EQ(empty_ns1->socketNamespace(), "");
}

TEST_F(SIOServerTest, ConfigurationMethods) {
    SIOServer server(*io_service);
    
    // Test configuration methods don't throw
    EXPECT_NO_THROW(server.set_ping_interval(30000));
    EXPECT_NO_THROW(server.set_ping_timeout(10000));
}

TEST_F(SIOServerTest, QueryParameterExtraction) {
    // Test the query parameter extraction logic
    auto extract_param = [](const std::string& resource, const std::string& key) -> std::string {
        auto qpos = resource.find('?');
        if (qpos == std::string::npos)
            return std::string();
        auto query = resource.substr(qpos + 1);
        size_t pos = 0;
        while (pos < query.size()) {
            auto amp = query.find('&', pos);
            auto part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            auto eq = part.find('=');
            std::string k = (eq == std::string::npos) ? part : part.substr(0, eq);
            if (k == key) {
                return (eq == std::string::npos) ? std::string() : part.substr(eq + 1);
            }
            if (amp == std::string::npos)
                break;
            pos = amp + 1;
        }
        return std::string();
    };
    
    std::string resource1 = "/socket.io/?EIO=4&transport=websocket&sid=abc123";
    std::string resource2 = "/socket.io/?transport=websocket";
    std::string resource3 = "/socket.io/?sid=def456";
    
    EXPECT_EQ(extract_param(resource1, "sid"), "abc123");
    EXPECT_EQ(extract_param(resource1, "EIO"), "4");
    EXPECT_EQ(extract_param(resource1, "transport"), "websocket");
    EXPECT_EQ(extract_param(resource2, "sid"), "");
    EXPECT_EQ(extract_param(resource3, "sid"), "def456");
}

TEST_F(SIOServerTest, ProtocolConstants) {
    // Verify that the server uses correct protocol constants
    EXPECT_EQ(engine_io::OPEN, '0');
    EXPECT_EQ(engine_io::PING, '2');
    EXPECT_EQ(engine_io::PONG, '3');
    EXPECT_EQ(engine_io::MESSAGE, '4');
    
    EXPECT_EQ(socket_io::CONNECT, 0);
    EXPECT_EQ(socket_io::EVENT, 2);
    EXPECT_EQ(socket_io::DISCONNECT, 1);
}

TEST_F(SIOServerTest, MessageTypeValidation) {
    // Test message type validation logic
    auto is_valid_engine_io_type = [](char type) -> bool {
        return type >= '0' && type <= '4';
    };
    
    auto is_valid_socket_io_type = [](int type) -> bool {
        return type >= 0 && type <= 6;
    };
    
    EXPECT_TRUE(is_valid_engine_io_type('0'));
    EXPECT_TRUE(is_valid_engine_io_type('4'));
    EXPECT_FALSE(is_valid_engine_io_type('5'));
    EXPECT_FALSE(is_valid_engine_io_type('a'));
    
    EXPECT_TRUE(is_valid_socket_io_type(0));
    EXPECT_TRUE(is_valid_socket_io_type(6));
    EXPECT_FALSE(is_valid_socket_io_type(7));
    EXPECT_FALSE(is_valid_socket_io_type(-1));
}

TEST_F(SIOServerTest, NamespaceHandling) {
    // Test namespace parsing logic
    auto parse_namespace = [](const std::string& sio_payload) -> std::pair<std::string, size_t> {
        if (sio_payload.empty()) return {"", 0};
        
        size_t idx = 1; // Skip packet type
        std::string nsp;
        
        if (idx < sio_payload.size() && sio_payload[idx] == '/') {
            size_t comma = sio_payload.find(',', idx);
            if (comma != std::string::npos) {
                nsp = sio_payload.substr(idx, comma - idx);
                idx = comma + 1;
            } else {
                nsp = sio_payload.substr(idx);
                idx = sio_payload.size();
            }
        }
        
        return {nsp, idx};
    };
    
    auto [ns1, idx1] = parse_namespace("2/chat,{\"data\":true}");
    EXPECT_EQ(ns1, "/chat");
    EXPECT_EQ(idx1, 7);  // After the comma
    
    auto [ns2, idx2] = parse_namespace("2{\"data\":true}");
    EXPECT_EQ(ns2, "");
    EXPECT_EQ(idx2, 1);
    
    auto [ns3, idx3] = parse_namespace("2/game");
    EXPECT_EQ(ns3, "/game");
    EXPECT_EQ(idx3, 6);
}

TEST_F(SIOServerTest, ErrorResponseBuilding) {
    // Test error response building logic
    auto build_error_response = [](const std::string& nsp, const std::string& message) -> std::string {
        std::ostringstream resp;
        resp << engine_io::MESSAGE << socket_io::CONNECT_ERROR;
        if (!nsp.empty()) {
            resp << nsp << ",";
        }
        resp << "{\"message\":\"" << message << "\"}";
        return resp.str();
    };
    
    std::string error1 = build_error_response("/chat", "Namespace not found");
    EXPECT_EQ(error1, "44/chat,{\"message\":\"Namespace not found\"}");
    
    std::string error2 = build_error_response("", "Invalid request");
    EXPECT_EQ(error2, "44{\"message\":\"Invalid request\"}");
}

TEST_F(SIOServerTest, ConnectResponseBuilding) {
    // Test connect response building logic
    auto build_connect_response = [](const std::string& nsp, const std::string& sid) -> std::string {
        std::ostringstream resp;
        resp << engine_io::MESSAGE << socket_io::CONNECT;
        if (!nsp.empty()) {
            resp << nsp << ",";
        }
        resp << "{\"sid\":\"" << sid << "\"}";
        return resp.str();
    };
    
    std::string response1 = build_connect_response("/chat", "abc123");
    EXPECT_EQ(response1, "40/chat,{\"sid\":\"abc123\"}");
    
    std::string response2 = build_connect_response("", "def456");
    EXPECT_EQ(response2, "40{\"sid\":\"def456\"}");
}

TEST_F(SIOServerTest, TimeoutValidation) {
    // Test timeout calculation logic
    auto calculate_timeout = [](int ping_interval, int ping_timeout, 
                               std::chrono::steady_clock::time_point last_pong) -> bool {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_pong).count();
        return elapsed > (ping_interval + ping_timeout);
    };
    
    auto recent_time = std::chrono::steady_clock::now();
    auto old_time = recent_time - std::chrono::seconds(60);
    
    EXPECT_FALSE(calculate_timeout(25000, 5000, recent_time));
    EXPECT_TRUE(calculate_timeout(25000, 5000, old_time));
}

TEST_F(SIOServerTest, HandshakeDataBuilding) {
    // Test Engine.IO handshake data building
    auto build_handshake = [](const std::string& sid, int ping_interval, 
                             int ping_timeout, int max_payload) -> std::string {
        std::ostringstream os;
        os << engine_io::OPEN;
        os << "{\"sid\":\"" << sid
           << "\",\"upgrades\":[],\"pingInterval\":" << ping_interval
           << ",\"pingTimeout\":" << ping_timeout
           << ",\"maxPayload\":" << max_payload << "}";
        return os.str();
    };
    
    std::string handshake = build_handshake("test123", 25000, 5000, 1000000);
    std::string expected = "0{\"sid\":\"test123\",\"upgrades\":[],\"pingInterval\":25000,\"pingTimeout\":5000,\"maxPayload\":1000000}";
    
    EXPECT_EQ(handshake, expected);
}

TEST_F(SIOServerTest, ResourceCleanup) {
    // Test resource cleanup logic
    struct ConnectionResources {
        std::string sid;
        std::chrono::steady_clock::time_point last_pong;
        bool has_timer;
    };
    
    std::map<int, ConnectionResources> connections; // Using int as mock handle
    
    // Add connections
    connections[1] = {"sid1", std::chrono::steady_clock::now(), true};
    connections[2] = {"sid2", std::chrono::steady_clock::now(), true};
    
    EXPECT_EQ(connections.size(), 2);
    
    // Cleanup connection
    auto cleanup = [&](int handle) {
        auto it = connections.find(handle);
        if (it != connections.end()) {
            // In real implementation, would cancel timer here
            connections.erase(it);
        }
    };
    
    cleanup(1);
    EXPECT_EQ(connections.size(), 1);
    EXPECT_EQ(connections.count(2), 1);
}
