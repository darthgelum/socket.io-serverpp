#ifndef SOCKETIO_SERVERPP_CONFIG_HPP
#define SOCKETIO_SERVERPP_CONFIG_HPP

#define SOCKETIO_SERVERPP_NAMESPACE socketio_serverpp

#include "common/cppconfig.hpp"

#include <boost/signals2.hpp>
#include <boost/asio.hpp>
#include <string>

namespace SOCKETIO_SERVERPP_NAMESPACE
{
namespace lib
{

typedef std::string SessionId;
typedef std::string Room;
namespace asio = boost::asio;

using boost::signals2::signal;

}
}

#endif // SOCKETIO_SERVERPP_CONFIG_HPP
