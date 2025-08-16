#include <gtest/gtest.h>
#include "Message.hpp"
#include "Constants.hpp"

using namespace socketio_serverpp::lib;

class MessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MessageTest, DefaultConstructor) {
    Message msg;
    
    EXPECT_FALSE(msg.isJson);
    EXPECT_TRUE(msg.sender.empty());
    EXPECT_EQ(msg.type, 0);
    EXPECT_EQ(msg.messageId, 0);
    EXPECT_FALSE(msg.idHandledByUser);
    EXPECT_TRUE(msg.endpoint.empty());
    EXPECT_TRUE(msg.data.empty());
}

TEST_F(MessageTest, ParameterizedConstructor) {
    Message msg(true, "test_sender", 2, 42, true, "/test", "test_data");
    
    EXPECT_TRUE(msg.isJson);
    EXPECT_EQ(msg.sender, "test_sender");
    EXPECT_EQ(msg.type, 2);
    EXPECT_EQ(msg.messageId, 42);
    EXPECT_TRUE(msg.idHandledByUser);
    EXPECT_EQ(msg.endpoint, "/test");
    EXPECT_EQ(msg.data, "test_data");
}

TEST_F(MessageTest, EmptyMethod) {
    Message empty_msg;
    EXPECT_TRUE(empty_msg.empty());
    
    Message non_empty_msg(false, "", 0, 0, false, "", "some_data");
    EXPECT_FALSE(non_empty_msg.empty());
}

TEST_F(MessageTest, SizeMethod) {
    Message msg;
    EXPECT_EQ(msg.size(), 0);
    
    msg.data = "hello";
    EXPECT_EQ(msg.size(), 5);
    
    msg.data = "hello world";
    EXPECT_EQ(msg.size(), 11);
}

TEST_F(MessageTest, IsAckMethod) {
    Message no_ack_msg;
    EXPECT_FALSE(no_ack_msg.is_ack());
    
    Message ack_msg(false, "", 0, 1, false, "", "");
    EXPECT_TRUE(ack_msg.is_ack());
    
    Message ack_msg2(false, "", 0, 42, false, "", "");
    EXPECT_TRUE(ack_msg2.is_ack());
}

TEST_F(MessageTest, JsonFlag) {
    Message text_msg(false, "", 0, 0, false, "", "plain text");
    EXPECT_FALSE(text_msg.isJson);
    
    Message json_msg(true, "", 0, 0, false, "", "{\"key\": \"value\"}");
    EXPECT_TRUE(json_msg.isJson);
}

TEST_F(MessageTest, MessageTypes) {
    // Test different message types (Socket.IO packet types)
    Message connect_msg(false, "", socket_io::CONNECT, 0, false, "", "");
    EXPECT_EQ(connect_msg.type, 0);
    
    Message disconnect_msg(false, "", socket_io::DISCONNECT, 0, false, "", "");
    EXPECT_EQ(disconnect_msg.type, 1);
    
    Message event_msg(false, "", socket_io::EVENT, 0, false, "", "");
    EXPECT_EQ(event_msg.type, 2);
}

TEST_F(MessageTest, EndpointHandling) {
    Message default_ns_msg(false, "", 0, 0, false, "", "");
    EXPECT_TRUE(default_ns_msg.endpoint.empty());
    
    Message custom_ns_msg(false, "", 0, 0, false, "/chat", "");
    EXPECT_EQ(custom_ns_msg.endpoint, "/chat");
}

TEST_F(MessageTest, LargeData) {
    std::string large_data(10000, 'X');
    Message msg(false, "", 0, 0, false, "", large_data);
    
    EXPECT_EQ(msg.data, large_data);
    EXPECT_EQ(msg.size(), 10000);
    EXPECT_FALSE(msg.empty());
}

TEST_F(MessageTest, SpecialCharacters) {
    std::string special_data = "测试\n\r\t\"'\\";
    Message msg(false, "", 0, 0, false, "", special_data);
    
    EXPECT_EQ(msg.data, special_data);
    EXPECT_EQ(msg.size(), special_data.size());
}

TEST_F(MessageTest, CopySemantics) {
    Message original(true, "sender", 2, 10, true, "/test", "data");
    Message copied = original;
    
    EXPECT_EQ(copied.isJson, original.isJson);
    EXPECT_EQ(copied.sender, original.sender);
    EXPECT_EQ(copied.type, original.type);
    EXPECT_EQ(copied.messageId, original.messageId);
    EXPECT_EQ(copied.idHandledByUser, original.idHandledByUser);
    EXPECT_EQ(copied.endpoint, original.endpoint);
    EXPECT_EQ(copied.data, original.data);
}
