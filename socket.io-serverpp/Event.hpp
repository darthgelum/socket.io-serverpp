#ifndef SOCKETIO_SERVERPP_EVENT_HPP
#define SOCKETIO_SERVERPP_EVENT_HPP

#include "config.hpp"
#include "Message.hpp"

#include "lib/rapidjson/document.h"

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

class Event
{
    public:
    Event(const string& event, const rapidjson::Document& json, const string& rawJson)
    :m_isJson(true), m_event(event), m_json(&json), m_stringdata(rawJson)
    {
    }
    
    Event(const string& event, const string& data)
    :m_isJson(false), m_event(event), m_json(0), m_stringdata(data)
    {
    }

    bool isJson() const
    {
        return m_isJson;
    }

    string name() const
    {
        return m_event;
    }

    string data() const
    {
        return m_stringdata;
    }

    bool   m_isJson;
    string m_event;
    const rapidjson::Document* m_json;
    string m_stringdata;
};

}

using lib::Event;

}

#endif // SOCKETIO_SERVERPP_EVENT_HPP
