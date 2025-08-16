#include <gtest/gtest.h>
#include "Constants.hpp"

using namespace socketio_serverpp::lib;

class ConstantsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ConstantsTest, EngineIOConstants) {
    EXPECT_EQ(engine_io::OPEN, '0');
    EXPECT_EQ(engine_io::CLOSE, '1');
    EXPECT_EQ(engine_io::PING, '2');
    EXPECT_EQ(engine_io::PONG, '3');
    EXPECT_EQ(engine_io::MESSAGE, '4');
}

TEST_F(ConstantsTest, SocketIOConstants) {
    EXPECT_EQ(socket_io::CONNECT, 0);
    EXPECT_EQ(socket_io::DISCONNECT, 1);
    EXPECT_EQ(socket_io::EVENT, 2);
    EXPECT_EQ(socket_io::ACK, 3);
    EXPECT_EQ(socket_io::CONNECT_ERROR, 4);
    EXPECT_EQ(socket_io::BINARY_EVENT, 5);
    EXPECT_EQ(socket_io::BINARY_ACK, 6);
}

TEST_F(ConstantsTest, ProtocolConstants) {
    EXPECT_STREQ(protocol::PROBE_PING, "2probe");
    EXPECT_STREQ(protocol::PROBE_PONG, "3probe");
    EXPECT_STREQ(protocol::ENGINE_IO_PING, "2");
    EXPECT_STREQ(protocol::ENGINE_IO_PONG, "3");
}

TEST_F(ConstantsTest, TimeoutConstants) {
    EXPECT_EQ(timeouts::DEFAULT_PING_INTERVAL, 25000);
    EXPECT_EQ(timeouts::DEFAULT_PING_TIMEOUT, 5000);
    EXPECT_EQ(timeouts::DEFAULT_HEARTBEAT, 30);
    EXPECT_EQ(timeouts::DEFAULT_CLOSE_TIME, 30);
    EXPECT_EQ(timeouts::DEFAULT_MAX_PAYLOAD, 1000000);
}

TEST_F(ConstantsTest, HttpStatusConstants) {
    EXPECT_STREQ(http_status::OK, "200 OK");
    EXPECT_STREQ(http_status::BAD_REQUEST, "400 Bad Request");
    EXPECT_STREQ(http_status::NOT_FOUND, "404 Not Found");
    EXPECT_STREQ(http_status::INTERNAL_ERROR, "500 Internal Server Error");
}

TEST_F(ConstantsTest, ConstantTypes) {
    // Test that constants are of expected types
    static_assert(std::is_same_v<decltype(engine_io::OPEN), const char>);
    static_assert(std::is_same_v<decltype(socket_io::CONNECT), const int>);
    static_assert(std::is_same_v<decltype(timeouts::DEFAULT_PING_INTERVAL), const int>);
}
