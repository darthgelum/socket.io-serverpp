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
#include <cctype>
#include <map>
#include <chrono>

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
    // Extract a query parameter from a resource string like "/socket.io/?EIO=4&transport=websocket&sid=ABC"
    static string get_query_param(const string& resource, const string& key)
    {
        auto qpos = resource.find('?');
        if (qpos == string::npos) return string();
        auto query = resource.substr(qpos + 1);
        size_t pos = 0;
        while (pos < query.size()) {
            auto amp = query.find('&', pos);
            auto part = query.substr(pos, amp == string::npos ? string::npos : amp - pos);
            auto eq = part.find('=');
            string k = (eq == string::npos) ? part : part.substr(0, eq);
            if (k == key) {
                return (eq == string::npos) ? string() : part.substr(eq + 1);
            }
            if (amp == string::npos) break;
            pos = amp + 1;
        }
        return string();
    }

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
        
        // Prefer sid from query string if provided (e.g. reconnect), otherwise generate a new one
        uuid = get_query_param(resource, "sid");
        if (uuid.empty()) {
            uuid = lib::uuid::uuid1();
        }

        // Engine.IO v4: upon WebSocket open, send OPEN packet (type '0') with handshake data
        // We run in WebSocket-only mode, so upgrades is empty
        m_conn_sid[hdl] = uuid; // store per-connection SID
    // initialize heartbeat tracking and schedule first ping
    m_last_pong[hdl] = std::chrono::steady_clock::now();
        std::ostringstream os;
        os << "0";
        os << "{\"sid\":\"" << uuid << "\",\"upgrades\":[],\"pingInterval\":"
           << m_pingInterval << ",\"pingTimeout\":" << m_pingTimeout << ",\"maxPayload\":"
           << m_maxPayload << "}";
        m_wsserver.send(hdl, os.str(), wspp::frame::opcode::value::text);

    schedule_ping(hdl);
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
        if (payload.empty()) return;

        char eio_type = payload[0];
        // Engine.IO v4 handling
        switch (eio_type) {
        case '2': // ping (may be '2' or '2probe')
                if (payload == "2probe") {
                    m_wsserver.send(hdl, "3probe", wspp::frame::opcode::value::text); // pong probe
                } else {
            // If client pings (not typical in v4), answer with pong and refresh liveness
                    m_wsserver.send(hdl, "3", wspp::frame::opcode::value::text);
            m_last_pong[hdl] = std::chrono::steady_clock::now();
                }
                return;
            case '3': { // pong from client
                m_last_pong[hdl] = std::chrono::steady_clock::now();
                return;
            }
            case '1': // close
                onWebsocketClose(hdl);
                return;
            case '4': // message (Socket.IO payload)
                break;
            default:
                // unsupported Engine.IO packet type
                return;
        }

        // Socket.IO v5 framing inside Engine.IO message
        string sio = payload.substr(1);
        if (sio.empty()) return;

        int pkt_type = sio[0] - '0';
        size_t idx = 1;
        string nsp;
        // optional namespace (starts with '/')
        if (idx < sio.size() && sio[idx] == '/') {
            size_t comma = sio.find(',', idx);
            if (comma != string::npos) {
                nsp = sio.substr(idx, comma - idx);
                idx = comma + 1;
            } else {
                nsp = sio.substr(idx);
                idx = sio.size();
            }
        }

        // optional ack id (digits) - skip for now
        while (idx < sio.size() && isdigit(static_cast<unsigned char>(sio[idx]))) ++idx;

        string data = (idx < sio.size()) ? sio.substr(idx) : string();

        auto socket_namespace = m_socket_namespace.find(nsp);
        if (socket_namespace == m_socket_namespace.end()) {
            // create on demand for default namespace
            if (nsp.empty()) {
                socket_namespace = m_socket_namespace.find("");
            } else {
                std::cout << "socketnamespace '" << nsp << "' not found" << std::endl;
            }
        }

        switch (pkt_type) {
            case 0: { // CONNECT
                if (socket_namespace != m_socket_namespace.end()) {
                    socket_namespace->second->onSocketIoConnection(hdl);
                    // respond CONNECT with sid
                    string sid;
                    auto it = m_conn_sid.find(hdl);
                    sid = (it != m_conn_sid.end()) ? it->second : lib::uuid::uuid1();
                    std::ostringstream resp;
                    resp << "40";
                    if (!nsp.empty()) resp << nsp << ",";
                    resp << "{\"sid\":\"" << sid << "\"}";
                    m_wsserver.send(hdl, resp.str(), wspp::frame::opcode::value::text);
                } else {
                    std::ostringstream resp;
                    resp << "44"; // CONNECT_ERROR
                    if (!nsp.empty()) resp << nsp << ",";
                    resp << "{\"message\":\"Namespace not found\"}";
                    m_wsserver.send(hdl, resp.str(), wspp::frame::opcode::value::text);
                }
                break;
            }
            case 1: { // DISCONNECT
                if (socket_namespace != m_socket_namespace.end()) {
                    socket_namespace->second->onSocketIoDisconnect(hdl);
                }
                break;
            }
            case 2: { // EVENT
                if (socket_namespace != m_socket_namespace.end()) {
                    Message message = { false, "", 2, 0, false, nsp, data };
                    socket_namespace->second->onSocketIoEvent(hdl, message);
                }
                break;
            }
            case 3: // ACK (not implemented)
            case 4: // CONNECT_ERROR (server shouldn't receive)
            case 5: // BINARY_EVENT (not implemented)
            case 6: // BINARY_ACK (not implemented)
            default:
                break;
        }
    }

    void onWebsocketClose(wspp::connection_hdl hdl)
    {
        for (auto sns : m_socket_namespace)
        {
            sns.second->onSocketIoDisconnect(hdl);
        }

        // cleanup heartbeat resources
        auto t = m_ping_timers.find(hdl);
        if (t != m_ping_timers.end()) {
            if (t->second) {
                boost::system::error_code ec;
                t->second->cancel(ec);
            }
            m_ping_timers.erase(t);
        }
        m_last_pong.erase(hdl);
        m_conn_sid.erase(hdl);
    }

private:
    void schedule_ping(wspp::connection_hdl hdl)
    {
        // Reuse a single timer per connection
        auto& ref = m_ping_timers[hdl];
        if (!ref) {
            ref = std::make_shared<asio::steady_timer>(m_io_service);
        }
        auto timer = ref;
        timer->expires_after(std::chrono::milliseconds(m_pingInterval));
        timer->async_wait([this, hdl](const boost::system::error_code& ec){
            if (ec) return; // canceled
            // Check timeout based on last pong
            auto it = m_last_pong.find(hdl);
            if (it != m_last_pong.end()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
                if (elapsed > (m_pingInterval + m_pingTimeout)) {
                    try {
                        m_wsserver.close(hdl, websocketpp::close::status::normal, "ping timeout");
                    } catch (...) {}
                    return;
                }
            }
            // send ping
            try {
                m_wsserver.send(hdl, "2", wspp::frame::opcode::value::text);
            } catch (...) { return; }
            // schedule next tick
            schedule_ping(hdl);
        });
    }
    asio::io_service&   m_io_service;
    wsserver            m_wsserver;
    scgiserver          m_scgiserver;
    std::shared_ptr<SocketNamespace> m_sockets;
    map<string, std::shared_ptr<SocketNamespace>> m_socket_namespace;
    boost::regex        m_reSockIoMsg;
    int                 m_heartBeat = 30;
    int                 m_closeTime = 30;
    vector<string>      m_protocols;
    // Engine.IO v4 settings (WebSocket-only)
    int                 m_pingInterval = 25000;
    int                 m_pingTimeout = 5000;
    int                 m_maxPayload = 1000000;
    // Per-connection Engine.IO session id
    map<wspp::connection_hdl, string, std::owner_less<wspp::connection_hdl>> m_conn_sid;
    // Heartbeat
    map<wspp::connection_hdl, std::shared_ptr<asio::steady_timer>, std::owner_less<wspp::connection_hdl>> m_ping_timers;
    map<wspp::connection_hdl, std::chrono::steady_clock::time_point, std::owner_less<wspp::connection_hdl>> m_last_pong;
};

}
    using lib::Server;
}

#endif // SOCKETIO_SERVERPP_SERVER_HPP
