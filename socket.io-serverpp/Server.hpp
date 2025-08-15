#include <functional>
#include <memory>
#ifndef SOCKETIO_SERVERPP_SERVER_HPP
#define SOCKETIO_SERVERPP_SERVER_HPP

#include "config.hpp"
#include "scgi/Service.h"
#include "Message.hpp"
#include "SocketNamespace.hpp"
#include "uuid.hpp"

#include <sstream>
#include <map>
#include <vector>
#include <string>

#include <boost/regex.hpp>

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class SocketNamespace;

typedef scgi::Service<asio::local::stream_protocol> scgiserver;

using std::map;
using std::vector;
using std::string;

class Server
{
    public:
    Server(asio::io_service& io_service)
    :m_io_service(io_service)
    ,m_reSockIoMsg("^(\\d):([\\d+]*):([^:]*):?(.*)", boost::regex::perl)
    {
        m_sockets = this->of("");

        m_protocols = {"websocket"};

        m_wsserver.init_asio(&io_service);
        m_wsserver.set_access_channels(websocketpp::log::alevel::none);
        m_wsserver.set_error_channels(websocketpp::log::elevel::warn);
    m_wsserver.set_message_handler(std::bind(&Server::onWebsocketMessage, this, std::placeholders::_1, std::placeholders::_2));
    m_wsserver.set_open_handler(std::bind(&Server::onWebsocketOpen, this, std::placeholders::_1));
    m_wsserver.set_close_handler(std::bind(&Server::onWebsocketClose, this, std::placeholders::_1));
        
    m_scgiserver.sig_RequestReceived.connect(std::bind(&Server::onScgiRequest, this, std::placeholders::_1));
    }

    void listen(const string& scgi_socket, int websocket_port)
    {
        auto acceptor = std::make_shared<scgiserver::proto::acceptor>(m_io_service, scgiserver::proto::endpoint(scgi_socket));
        m_scgiserver.listen(acceptor);
        m_scgiserver.start_accept();

        m_wsserver.listen(websocket_port);
        m_wsserver.start_accept();
    }

    std::shared_ptr<SocketNamespace> of(const string& nsp)
    {
        auto iter = m_socket_namespace.find(nsp);
        if (iter == m_socket_namespace.end())
        {
                auto snsp = std::make_shared<SocketNamespace>(nsp, m_wsserver);
            m_socket_namespace.insert(std::make_pair(nsp, snsp));
            return snsp;
        }
        else
        {
            return iter->second;
        }
    }

    std::shared_ptr<SocketNamespace> sockets()
    {
        return m_sockets;
    }

    void run()
    {
        m_io_service.run();
    }

    private:
    void onScgiRequest(scgiserver::CRequestPtr req)
    {
        string uri = req->header("REQUEST_URI");
        string uuid = uuid::uuid1();
//        std::cout << "Req: " << req->header("REQUEST_METHOD") << "  Uri: " << uri << std::endl;

        if (uri.find("/socket.io/1/") == 0)
        {
            std::ostringstream os;
            os << "Status: 200 OK\r\n";
            os << "Content-Type: text/plain\r\n\r\n";
            os << uuid + ":";
            if (m_heartBeat > 0)
            {
                os << m_heartBeat;
            }
            os << ":" << m_closeTime << ":";
            for (const auto& p : m_protocols)
            {
                os << p << ",";
            }

            req->writeData(os.str());
            req->asyncClose(std::bind([](scgiserver::CRequestPtr req){ cout << "closed" << endl;}, req));
        }
    }

    
    void onWebsocketOpen(wspp::connection_hdl hdl)
    {
        auto connection = m_wsserver.get_con_from_hdl(hdl);
        string resource = connection->get_resource();
        string uuid;
        
        // Extract UUID from resource path, with bounds checking
        if (resource.length() > 23) {
            uuid = resource.substr(23);
        } else {
            // Generate a new UUID if the resource doesn't contain one
            uuid = lib::uuid::uuid1();
        }

        //m_websocketServer.send(hdl, "5::{name:'connect', args={}}", ws::frame::opcode::value::text);
        m_wsserver.send(hdl, "1::", wspp::frame::opcode::value::text);
        
        //    m_websocketServer.set_timer(10*1000, bind(&SocketIoServer::sendHeartbeart, this, hdl));
        //    m_websocketServer.set_timer(1*1000, bind(&SocketIoServer::customEvent, this, hdl));
    }

    /*
     * [message type] ':' [message id ('+')] ':' [message endpoint] (':' [message data])
     *
     * 0::/test disconnect a socket to /test endpoint
     * 0 disconnect whole socket
     * 1::/test?my=param
     * 3:<id>:<ep>:<data>
     * 4:<id>:<ep>:<json>
     * 5:<id>:<ep>:<json event>
     * 6:::<id>
     *
     * regex: "\d:\d*\+*:[^:]*:.*"
     */

    void onWebsocketMessage(wspp::connection_hdl hdl, wsserver::message_ptr msg)
    {
        string payload = msg->get_payload();
        if (payload.size() < 3)
            return;

        boost::smatch match;
        if (boost::regex_match(payload, match, m_reSockIoMsg))
        {
            Message message = {
                false,
                "",
                payload[0] - '0',
                0,
                false,
                match[3],
                match[4]
            };
                        
            auto socket_namespace = m_socket_namespace.find(message.endpoint);
            if (socket_namespace == m_socket_namespace.end())
            {
                std::cout << "socketnamespace '" << message.endpoint << "' not found" << std::endl;
                return;
            }

            switch(payload[0])
            {
                case '0': // Disconnect
                    break;
                case '1': // Connect
                    // signal connect to matching namespace
                    socket_namespace->second->onSocketIoConnection(hdl);
                    m_wsserver.send(hdl, payload, wspp::frame::opcode::value::text);
                    break;
                case '4': // JsonMessage
                    message.isJson = true;
                    // falltrough
                case '3': // Message
                    socket_namespace->second->onSocketIoMessage(hdl, message);
                    break;
                case '5': // Event
                    socket_namespace->second->onSocketIoEvent(hdl, message);
                    break;
            }
        }
        //std::cout << msg->get_payload() << std::endl;
    }

    void onWebsocketClose(wspp::connection_hdl hdl)
    {
        for (auto sns : m_socket_namespace)
        {
            sns.second->onSocketIoDisconnect(hdl);
        }
    }

private:
    asio::io_service&   m_io_service;
    wsserver            m_wsserver;
    scgiserver          m_scgiserver;
    std::shared_ptr<SocketNamespace> m_sockets;
    map<string, std::shared_ptr<SocketNamespace>> m_socket_namespace;
    boost::regex        m_reSockIoMsg;
    int                 m_heartBeat = 30;
    int                 m_closeTime = 30;
    vector<string>      m_protocols;
};

}
    using lib::Server;
}

#endif // SOCKETIO_SERVERPP_SERVER_HPP
