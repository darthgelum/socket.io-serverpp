#include <gtest/gtest.h>
#include "Error.hpp"
#include <system_error>

using namespace socketio_serverpp::lib;

class ErrorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ErrorTest, SocketIOExceptionBasic) {
    const std::string message = "Test error message";
    SocketIOException ex(message);
    
    EXPECT_STREQ(ex.what(), message.c_str());
    EXPECT_EQ(ex.error_code(), SocketIOErrorCode::PROTOCOL_ERROR);
}

TEST_F(ErrorTest, SocketIOExceptionWithErrorCode) {
    const std::string message = "Invalid namespace error";
    SocketIOException ex(message, SocketIOErrorCode::INVALID_NAMESPACE);
    
    EXPECT_STREQ(ex.what(), message.c_str());
    EXPECT_EQ(ex.error_code(), SocketIOErrorCode::INVALID_NAMESPACE);
}

TEST_F(ErrorTest, ErrorCodeValues) {
    EXPECT_EQ(static_cast<int>(SocketIOErrorCode::SUCCESS), 0);
    EXPECT_EQ(static_cast<int>(SocketIOErrorCode::INVALID_NAMESPACE), 1);
    EXPECT_EQ(static_cast<int>(SocketIOErrorCode::CONNECTION_FAILED), 2);
    EXPECT_EQ(static_cast<int>(SocketIOErrorCode::MESSAGE_PARSE_ERROR), 3);
    EXPECT_EQ(static_cast<int>(SocketIOErrorCode::TIMEOUT), 4);
    EXPECT_EQ(static_cast<int>(SocketIOErrorCode::PROTOCOL_ERROR), 5);
    EXPECT_EQ(static_cast<int>(SocketIOErrorCode::RESOURCE_EXHAUSTED), 6);
}

TEST_F(ErrorTest, ErrorCategory) {
    const auto& category = socket_io_category();
    
    EXPECT_STREQ(category.name(), "socket_io");
    
    // Test error messages
    EXPECT_EQ(category.message(static_cast<int>(SocketIOErrorCode::SUCCESS)), "Success");
    EXPECT_EQ(category.message(static_cast<int>(SocketIOErrorCode::INVALID_NAMESPACE)), "Invalid namespace");
    EXPECT_EQ(category.message(static_cast<int>(SocketIOErrorCode::CONNECTION_FAILED)), "Connection failed");
    EXPECT_EQ(category.message(static_cast<int>(SocketIOErrorCode::MESSAGE_PARSE_ERROR)), "Message parse error");
    EXPECT_EQ(category.message(static_cast<int>(SocketIOErrorCode::TIMEOUT)), "Operation timeout");
    EXPECT_EQ(category.message(static_cast<int>(SocketIOErrorCode::PROTOCOL_ERROR)), "Protocol error");
    EXPECT_EQ(category.message(static_cast<int>(SocketIOErrorCode::RESOURCE_EXHAUSTED)), "Resource exhausted");
}

TEST_F(ErrorTest, MakeErrorCode) {
    auto ec = make_error_code(SocketIOErrorCode::INVALID_NAMESPACE);
    
    EXPECT_EQ(ec.value(), static_cast<int>(SocketIOErrorCode::INVALID_NAMESPACE));
    EXPECT_EQ(&ec.category(), &socket_io_category());
    EXPECT_EQ(ec.message(), "Invalid namespace");
}

TEST_F(ErrorTest, ErrorCodeComparison) {
    auto ec1 = make_error_code(SocketIOErrorCode::SUCCESS);
    auto ec2 = make_error_code(SocketIOErrorCode::SUCCESS);
    auto ec3 = make_error_code(SocketIOErrorCode::TIMEOUT);
    
    EXPECT_EQ(ec1, ec2);
    EXPECT_NE(ec1, ec3);
    EXPECT_FALSE(ec1); // SUCCESS should be false in boolean context
    EXPECT_TRUE(ec3);  // Non-success should be true in boolean context
}

TEST_F(ErrorTest, ErrorCodeIntegration) {
    // Test that we can use SocketIOErrorCode with std::error_code
    std::error_code ec = SocketIOErrorCode::CONNECTION_FAILED;
    
    EXPECT_EQ(ec.value(), static_cast<int>(SocketIOErrorCode::CONNECTION_FAILED));
    EXPECT_EQ(ec.message(), "Connection failed");
}

TEST_F(ErrorTest, ExceptionInheritance) {
    SocketIOException ex("test", SocketIOErrorCode::TIMEOUT);
    
    // Should be catchable as std::runtime_error
    try {
        throw ex;
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "test");
    } catch (...) {
        FAIL() << "Exception should be catchable as std::runtime_error";
    }
}
