#ifndef SOCKETIO_SERVERPP_CONFIG_HPP
#define SOCKETIO_SERVERPP_CONFIG_HPP

#define SOCKETIO_SERVERPP_NAMESPACE socketio_serverpp

#include "common/cppconfig.hpp"

#define _WEBSOCKETPP_CPP11_STL_
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <boost/signals2.hpp>

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

typedef string SessionId;
typedef string Room;
namespace asio = boost::asio;
namespace wspp = websocketpp;

using boost::signals2::signal;

typedef wspp::server<wspp::config::asio> wsserver;

}
}

#endif // SOCKETIO_SERVERPP_CONFIG_HPP
