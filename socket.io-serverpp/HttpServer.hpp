#ifndef SOCKETIO_SERVERPP_HTTPSERVER_HPP
#define SOCKETIO_SERVERPP_HTTPSERVER_HPP

#include "config.hpp"
#include "Logger.hpp"
#include "Constants.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/signals2.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace SOCKETIO_SERVERPP_NAMESPACE {
namespace lib {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

/**
 * @brief HTTP request representation for Socket.IO handshakes and polling
 */
class HttpRequest {
public:
    HttpRequest(const http::request<http::string_body>& req) 
        : m_request(req) {}
    
    std::string method() const {
        return std::string(m_request.method_string());
    }
    
    std::string uri() const {
        return std::string(m_request.target());
    }
    
    std::string header(const std::string& name) const {
        auto it = m_request.find(name);
        return (it != m_request.end()) ? std::string(it->value()) : std::string();
    }
    
    std::string body() const {
        return m_request.body();
    }
    
    http::verb http_method() const {
        return m_request.method();
    }
    
    const http::request<http::string_body>& raw_request() const {
        return m_request;
    }

private:
    const http::request<http::string_body>& m_request;
};

/**
 * @brief HTTP response helper for Socket.IO responses
 */
class HttpResponse {
public:
    HttpResponse() {
        m_response.version(11);
        m_response.result(http::status::ok);
        m_response.set(http::field::server, "socket.io-serverpp");
        m_response.set(http::field::content_type, "text/plain");
        // CORS headers for browser compatibility
        m_response.set(http::field::access_control_allow_origin, "*");
        m_response.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        m_response.set(http::field::access_control_allow_headers, "Content-Type");
    }
    
    void set_status(http::status status) {
        m_response.result(status);
    }
    
    void set_header(const std::string& name, const std::string& value) {
        m_response.set(name, value);
    }
    
    void set_body(const std::string& body) {
        m_response.body() = body;
        m_response.prepare_payload();
    }
    
    void set_content_type(const std::string& content_type) {
        m_response.set(http::field::content_type, content_type);
    }
    
    http::response<http::string_body>& raw_response() {
        return m_response;
    }

private:
    http::response<http::string_body> m_response;
};

/**
 * @brief HTTP session handling a single client connection
 */
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket&& socket, 
                std::function<void(const HttpRequest&, HttpResponse&)> request_handler)
        : m_stream(std::move(socket))
        , m_request_handler(std::move(request_handler)) {
    }
    
    void start() {
        net::dispatch(m_stream.get_executor(),
                     beast::bind_front_handler(&HttpSession::do_read,
                                              shared_from_this()));
    }

private:
    void do_read() {
        // Make the request empty before reading,
        // otherwise the operation behavior is undefined.
        m_request = {};
        
        // Set the timeout.
        m_stream.expires_after(std::chrono::seconds(30));
        
        // Read a request
        http::async_read(m_stream, m_buffer, m_request,
            beast::bind_front_handler(&HttpSession::on_read,
                                     shared_from_this()));
    }
    
    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        
        // This means they closed the connection
        if (ec == http::error::end_of_stream) {
            return do_close();
        }
        
        if (ec) {
            LOG_WARN("HTTP read error: ", ec.message());
            return;
        }
        
        // Handle the request
        handle_request();
    }
    
    void handle_request() {
        try {
            HttpRequest req(m_request);
            HttpResponse resp;
            
            // Call the request handler
            m_request_handler(req, resp);
            
            // Send the response
            send_response(std::move(resp.raw_response()));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling HTTP request: ", e.what());
            send_bad_request("Internal server error");
        }
    }
    
    void send_response(http::response<http::string_body>&& response) {
        auto sp = std::make_shared<http::response<http::string_body>>(std::move(response));
        m_response = sp;
        
        // Write the response
        http::async_write(m_stream, *m_response,
            beast::bind_front_handler(&HttpSession::on_write,
                                     shared_from_this(),
                                     m_response->need_eof()));
    }
    
    void send_bad_request(const std::string& why) {
        http::response<http::string_body> res{http::status::bad_request, m_request.version()};
        res.set(http::field::server, "socket.io-serverpp");
        res.set(http::field::content_type, "text/html");
        res.keep_alive(m_request.keep_alive());
        res.body() = "Error: " + why;
        res.prepare_payload();
        
        send_response(std::move(res));
    }
    
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        
        if (ec) {
            LOG_WARN("HTTP write error: ", ec.message());
            return;
        }
        
        if (close) {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return do_close();
        }
        
        // We're done with the response so delete it
        m_response = nullptr;
        
        // Read another request
        do_read();
    }
    
    void do_close() {
        // Send a TCP shutdown
        beast::error_code ec;
        m_stream.socket().shutdown(tcp::socket::shutdown_send, ec);
        
        // At this point the connection is closed gracefully
    }

private:
    beast::tcp_stream m_stream;
    beast::flat_buffer m_buffer;
    http::request<http::string_body> m_request;
    std::shared_ptr<http::response<http::string_body>> m_response;
    std::function<void(const HttpRequest&, HttpResponse&)> m_request_handler;
};

/**
 * @brief HTTP server for Socket.IO handshakes and long polling
 */
class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    explicit HttpServer(net::io_context& ioc)
        : m_ioc(ioc)
        , m_acceptor(net::make_strand(ioc)) {
    }
    
    /**
     * @brief Start listening on the specified address and port
     * @param address IP address to bind to
     * @param port Port number to listen on
     */
    void listen(const std::string& address, unsigned short port) {
        beast::error_code ec;
        
        // Open the acceptor
        m_acceptor.open(tcp::v4(), ec);
        if (ec) {
            LOG_ERROR("Failed to open acceptor: ", ec.message());
            throw std::runtime_error("Failed to open HTTP acceptor: " + ec.message());
        }
        
        // Allow address reuse
        m_acceptor.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            LOG_ERROR("Failed to set reuse_address: ", ec.message());
            throw std::runtime_error("Failed to set socket options: " + ec.message());
        }
        
        // Bind to the server address
        auto const addr = net::ip::make_address(address);
        m_acceptor.bind({addr, port}, ec);
        if (ec) {
            LOG_ERROR("Failed to bind to ", address, ":", port, " - ", ec.message());
            throw std::runtime_error("Failed to bind HTTP server: " + ec.message());
        }
        
        // Start listening for connections
        m_acceptor.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            LOG_ERROR("Failed to listen: ", ec.message());
            throw std::runtime_error("Failed to listen on HTTP server: " + ec.message());
        }
        
        LOG_INFO("HTTP server listening on ", address, ":", port);
    }
    
    /**
     * @brief Start accepting connections
     */
    void start_accept() {
        do_accept();
    }
    
    /**
     * @brief Set the request handler function
     * @param handler Function to handle incoming HTTP requests
     */
    void set_request_handler(std::function<void(const HttpRequest&, HttpResponse&)> handler) {
        m_request_handler = std::move(handler);
    }
    
    /**
     * @brief Stop the server
     */
    void stop() {
        beast::error_code ec;
        m_acceptor.close(ec);
        if (ec) {
            LOG_WARN("Error closing HTTP acceptor: ", ec.message());
        }
    }

private:
    void do_accept() {
        // The new connection gets its own strand
        m_acceptor.async_accept(
            net::make_strand(m_ioc),
            beast::bind_front_handler(&HttpServer::on_accept,
                                     shared_from_this()));
    }
    
    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            LOG_WARN("HTTP accept error: ", ec.message());
        } else {
            // Create the session and run it
            std::make_shared<HttpSession>(std::move(socket), m_request_handler)->start();
        }
        
        // Accept another connection
        do_accept();
    }

private:
    net::io_context& m_ioc;
    tcp::acceptor m_acceptor;
    std::function<void(const HttpRequest&, HttpResponse&)> m_request_handler;
};

} // namespace lib
} // namespace SOCKETIO_SERVERPP_NAMESPACE

#endif // SOCKETIO_SERVERPP_HTTPSERVER_HPP
