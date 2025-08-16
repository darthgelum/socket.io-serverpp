#include <gtest/gtest.h>
#include "Logger.hpp"
#include <sstream>
#include <streambuf>

using namespace socketio_serverpp::lib;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Capture stdout
        original_cout_buf = std::cout.rdbuf();
        std::cout.rdbuf(captured_cout.rdbuf());
        
        // Reset logger to default state
        Logger::instance().set_level(LogLevel::INFO);
    }
    
    void TearDown() override {
        // Restore stdout
        std::cout.rdbuf(original_cout_buf);
    }
    
    std::string getCapturedOutput() {
        return captured_cout.str();
    }
    
    void clearCapturedOutput() {
        captured_cout.str("");
        captured_cout.clear();
    }

private:
    std::stringstream captured_cout;
    std::streambuf* original_cout_buf;
};

TEST_F(LoggerTest, SingletonPattern) {
    Logger& logger1 = Logger::instance();
    Logger& logger2 = Logger::instance();
    
    EXPECT_EQ(&logger1, &logger2);
}

TEST_F(LoggerTest, LogLevelSetting) {
    Logger& logger = Logger::instance();
    
    logger.set_level(LogLevel::DEBUG);
    EXPECT_EQ(logger.get_level(), LogLevel::DEBUG);
    
    logger.set_level(LogLevel::ERROR);
    EXPECT_EQ(logger.get_level(), LogLevel::ERROR);
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger& logger = Logger::instance();
    
    // Set to WARN level - should only show WARN, ERROR, FATAL
    logger.set_level(LogLevel::WARN);
    
    clearCapturedOutput();
    logger.trace("trace message");
    logger.debug("debug message");
    logger.info("info message");
    EXPECT_TRUE(getCapturedOutput().empty());
    
    clearCapturedOutput();
    logger.warn("warn message");
    std::string output = getCapturedOutput();
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("warn message"), std::string::npos);
    EXPECT_NE(output.find("[WARN ]"), std::string::npos);
}

TEST_F(LoggerTest, LogFormatting) {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::TRACE);
    
    clearCapturedOutput();
    logger.info("test message");
    std::string output = getCapturedOutput();
    
    // Check that output contains expected components
    EXPECT_NE(output.find("[INFO ]"), std::string::npos);
    EXPECT_NE(output.find("test message"), std::string::npos);
    
    // Check timestamp format (YYYY-MM-DD HH:MM:SS.mmm)
    EXPECT_TRUE(output.find("20") != std::string::npos); // Year starts with 20
    EXPECT_TRUE(output.find(":") != std::string::npos);  // Time separators
    EXPECT_TRUE(output.find(".") != std::string::npos);  // Milliseconds separator
}

TEST_F(LoggerTest, DifferentLogLevels) {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::TRACE);
    
    clearCapturedOutput();
    logger.trace("trace test");
    EXPECT_NE(getCapturedOutput().find("[TRACE]"), std::string::npos);
    
    clearCapturedOutput();
    logger.debug("debug test");
    EXPECT_NE(getCapturedOutput().find("[DEBUG]"), std::string::npos);
    
    clearCapturedOutput();
    logger.info("info test");
    EXPECT_NE(getCapturedOutput().find("[INFO ]"), std::string::npos);
    
    clearCapturedOutput();
    logger.warn("warn test");
    EXPECT_NE(getCapturedOutput().find("[WARN ]"), std::string::npos);
    
    clearCapturedOutput();
    logger.error("error test");
    EXPECT_NE(getCapturedOutput().find("[ERROR]"), std::string::npos);
    
    clearCapturedOutput();
    logger.fatal("fatal test");
    EXPECT_NE(getCapturedOutput().find("[FATAL]"), std::string::npos);
}

TEST_F(LoggerTest, MultipleArguments) {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::INFO);
    
    clearCapturedOutput();
    logger.info("Value: ", 42, " String: ", "test", " Float: ", 3.14);
    std::string output = getCapturedOutput();
    
    EXPECT_NE(output.find("Value: 42"), std::string::npos);
    EXPECT_NE(output.find("String: test"), std::string::npos);
    EXPECT_NE(output.find("Float: 3.14"), std::string::npos);
}

TEST_F(LoggerTest, LogMacros) {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::TRACE);
    
    clearCapturedOutput();
    LOG_INFO("macro test");
    std::string output = getCapturedOutput();
    
    EXPECT_NE(output.find("[INFO ]"), std::string::npos);
    EXPECT_NE(output.find("macro test"), std::string::npos);
}

TEST_F(LoggerTest, EmptyMessage) {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::INFO);
    
    clearCapturedOutput();
    logger.info("");
    std::string output = getCapturedOutput();
    
    EXPECT_NE(output.find("[INFO ]"), std::string::npos);
    // Should still have timestamp and level, even with empty message
}

TEST_F(LoggerTest, LongMessage) {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::INFO);
    
    std::string long_message(1000, 'A');
    
    clearCapturedOutput();
    logger.info(long_message);
    std::string output = getCapturedOutput();
    
    EXPECT_NE(output.find(long_message), std::string::npos);
}

TEST_F(LoggerTest, LogLevelHierarchy) {
    Logger& logger = Logger::instance();
    
    // Test that each level allows higher levels
    logger.set_level(LogLevel::ERROR);
    
    clearCapturedOutput();
    logger.trace("should not appear");
    logger.debug("should not appear");
    logger.info("should not appear");
    logger.warn("should not appear");
    logger.error("should appear");
    logger.fatal("should appear");
    
    std::string output = getCapturedOutput();
    EXPECT_EQ(output.find("should not appear"), std::string::npos);
    EXPECT_NE(output.find("should appear"), std::string::npos);
}
