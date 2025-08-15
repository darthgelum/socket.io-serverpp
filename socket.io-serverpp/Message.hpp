#ifndef SOCKETIO_SERVERPP_MESSAGE_HPP
#define SOCKETIO_SERVERPP_MESSAGE_HPP

#include "config.hpp"

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

// encoding text,json
struct Message
{
    bool isJson;
    string sender;

    int type;
    int messageId;
    bool idHandledByUser;
    string endpoint;
    string data;
};

}

using lib::Message;

}

#endif // SOCKETIO_SERVERPP_MESSAGE_HPP
