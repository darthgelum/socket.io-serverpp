#pragma once

#include "config.hpp"
#include "Message.hpp"
#include "Event.hpp"

#include "lib/rapidjson/document.h"

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class SocketNamespace;

class Socket
{
    friend SocketNamespace;
    public:
    Socket(wsserver& wsserver, const string& nsp, wspp::connection_hdl hdl)
    :m_wsserver(wsserver)
    ,m_namespace(nsp)
    ,m_ws_hdl(hdl)
    {
    }

    void on(const std::string& event, std::function<void (const Event& data)> cb)
    {
//        cout << "register event '" << event << "'" << this << endl;
        m_events[event] = cb;
    }

/*
    void on(const std::string& event, std::function<void (const string& from, const string& data)> cb)
    {
    }
    */
    
    void send(const string& data)
    {
        // Socket.IO v5: EVENT packet with raw payload array or string
        string pl = "42"; // 4 = Engine.IO message, 2 = Socket.IO EVENT
        if (!m_namespace.empty()) {
            pl += m_namespace + ",";
        }
        pl += data; // caller must pass a valid JSON (e.g. ["event", ...])
        m_wsserver.send(m_ws_hdl, pl, wspp::frame::opcode::value::text);
//        cout << "Socket send: " << data << endl;
    }

    void emit(const string& name, const string& data)
    {
        // Socket.IO v5: EVENT packet with [name, ...args]
        string pl = "42"; // 4 + 2
        if (!m_namespace.empty()) {
            pl += m_namespace + ",";
        }
        pl += "[\"" + name + "\"," + data + "]";
        m_wsserver.send(m_ws_hdl, pl, wspp::frame::opcode::value::text);
//        cout << "Socket emit: " << name << " data: " << data << endl;
    }

    void onMessage(const Message& msg)
    {
        auto iter = m_events.find("message");
        if (iter != m_events.end())
        {
            iter->second({"message", msg.data});
        }
    }

    void onEvent(const string& event, const rapidjson::Document& json, const string& rawJson)
    {
//        cout << "Socket check '" << event << "'" << this << endl;
        auto iter = m_events.find(event);
        if (iter != m_events.end())
        {
            iter->second({event, json, rawJson});
//            cout << "Socket event: " << event << " matched" << endl;
        }
    }

    string uuid() const
    {
        auto connection = m_wsserver.get_con_from_hdl(m_ws_hdl);
        return connection->get_resource().substr(23);
    }

    wsserver&               m_wsserver;
    const string&           m_namespace;
    wspp::connection_hdl    m_ws_hdl;
    map<string, std::function<void (const Event&)>> m_events;
};

}
    using lib::Socket;
}
