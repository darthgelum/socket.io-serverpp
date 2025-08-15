#include <memory>
#ifndef SOCKETIO_SERVERPP_SOCKETNAMESPACE_HPP
#define SOCKETIO_SERVERPP_SOCKETNAMESPACE_HPP

#include "config.hpp"
#include "Server.hpp"
#include "Socket.hpp"

#include "lib/rapidjson/document.h"

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class Server;
class Socket;

class SocketNamespace
{
    friend Server;
    public:
    SocketNamespace(const string& nsp, wsserver& server)
    :m_namespace(nsp)
    ,m_wsserver(server)
    {
    }

    void onConnection(std::function<void (Socket&)> cb)
    {
        sig_Connection.connect(cb);
    }

    void onDisconnection(std::function<void (Socket&)> cb)
    {
        sig_Disconnection.connect(cb);
    }

#if 0
    void on(const string& event, std::function<void (const string&)> cb)
    {
        // add cb to event-signal
        //m_events[event]; //.connect(cb);
    }
#endif

    //list<Client> clients(const string& room) const;
    //void except(const SessionId& id);
    void send(const string& data)
    {
//        cout << "SocketNamespace send: " << data << endl;
        for (const auto& i : m_sockets)
        {
            i.second->send(data);
        }
    }

    void emit(const string& name, const string& data)
    {
//        cout << "SocketNamespace emit: " << name << " data: " << data << endl;
        for (const auto& i : m_sockets)
        {
            i.second->emit(name, data);
        }
    }

    string socketNamespace() const
    {
        return m_namespace;
    }
    
    private:

    void onSocketIoConnection(wspp::connection_hdl hdl)
    {
    auto socket = std::make_shared<Socket>(m_wsserver, m_namespace, hdl);
        m_sockets[hdl] = socket;
        sig_Connection(*socket);
    }

    void onSocketIoMessage(wspp::connection_hdl hdl, const Message& msg)
    {
//        cout << "SocketNamespace(" << m_namespace << ") msg: " << msg.data << endl;
        auto iter = m_sockets.find(hdl);
        if (iter != m_sockets.end())
        {
            iter->second->onMessage(msg);
        }
    }
                    
    void onSocketIoEvent(wspp::connection_hdl hdl, const Message& msg)
    {
        rapidjson::Document json;
        json.Parse<0>(msg.data.c_str());

        string name = json["name"].GetString();
//        string args = json["args"].GetString();
        string args;

//        cout << "SocketNamespace(" << m_namespace << ") event: " << name << " with args " << args << endl;

        auto iter = m_sockets.find(hdl);
        if (iter != m_sockets.end())
        {
            // First, deliver to the sending socket's event handlers
            iter->second->onEvent(name, json, msg.data);
            
            // Then broadcast to ALL other connected sockets in this namespace
            // This is what makes multi-tab chat work
            for (const auto& socket_pair : m_sockets)
            {
                // Don't send back to sender - compare the handles directly
                if (!socket_pair.first.owner_before(hdl) && !hdl.owner_before(socket_pair.first))
                {
                    continue; // This is the sender, skip
                }
                
                // Re-broadcast the exact same event to other clients
                string payload = "5::" + m_namespace + ":" + msg.data;
                m_wsserver.send(socket_pair.first, payload, wspp::frame::opcode::value::text);
            }
        }

    }

    void onSocketIoDisconnect(wspp::connection_hdl hdl)
    {
        auto iter = m_sockets.find(hdl);
        if (iter != m_sockets.end())
        {
            sig_Disconnection(*(iter->second));
            m_sockets.erase(iter);
        }
    }

    string m_namespace;
    wsserver& m_wsserver;

    signal<void (Socket&)> sig_Connection;
    signal<void (Socket&)> sig_Disconnection;
    map<wspp::connection_hdl, std::shared_ptr<Socket>, std::owner_less<wspp::connection_hdl>> m_sockets;
};

}
    using lib::SocketNamespace;
}

#endif // SOCKETIO_SERVERPP_SOCKETNAMESPACE_HPP
