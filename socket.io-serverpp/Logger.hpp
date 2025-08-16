#ifndef SOCKETIO_SERVERPP_LOGGER_HPP
#define SOCKETIO_SERVERPP_LOGGER_HPP

#include "config.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <chrono>
#include <iomanip>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    void set_level(LogLevel level) {
        current_level_ = level;
    }
    
    LogLevel get_level() const {
        return current_level_;
    }
    
    template<typename... Args>
    void log(LogLevel level, const string& format, Args&&... args) {
        if (level < current_level_) {
            return;
        }
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        oss << " [" << level_to_string(level) << "] ";
        
        format_message(oss, format, std::forward<Args>(args)...);
        
        std::cout << oss.str() << std::endl;
    }
    
    template<typename... Args>
    void trace(const string& format, Args&&... args) {
        log(LogLevel::TRACE, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void debug(const string& format, Args&&... args) {
        log(LogLevel::DEBUG, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void info(const string& format, Args&&... args) {
        log(LogLevel::INFO, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void warn(const string& format, Args&&... args) {
        log(LogLevel::WARN, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void error(const string& format, Args&&... args) {
        log(LogLevel::ERROR, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void fatal(const string& format, Args&&... args) {
        log(LogLevel::FATAL, format, std::forward<Args>(args)...);
    }

private:
    Logger() : current_level_(LogLevel::INFO) {}
    
    LogLevel current_level_;
    
    string level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
    
    template<typename T>
    void format_message(std::ostringstream& oss, const T& value) {
        oss << value;
    }
    
    template<typename T, typename... Args>
    void format_message(std::ostringstream& oss, const T& value, Args&&... args) {
        oss << value;
        format_message(oss, std::forward<Args>(args)...);
    }
};

// Convenience macros for logging
#define LOG_TRACE(...) SOCKETIO_SERVERPP_NAMESPACE::lib::Logger::instance().trace(__VA_ARGS__)
#define LOG_DEBUG(...) SOCKETIO_SERVERPP_NAMESPACE::lib::Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...)  SOCKETIO_SERVERPP_NAMESPACE::lib::Logger::instance().info(__VA_ARGS__)
#define LOG_WARN(...)  SOCKETIO_SERVERPP_NAMESPACE::lib::Logger::instance().warn(__VA_ARGS__)
#define LOG_ERROR(...) SOCKETIO_SERVERPP_NAMESPACE::lib::Logger::instance().error(__VA_ARGS__)
#define LOG_FATAL(...) SOCKETIO_SERVERPP_NAMESPACE::lib::Logger::instance().fatal(__VA_ARGS__)

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_LOGGER_HPP
