#ifndef SOCKETIO_SERVERPP_SCGI_SERVICE_H
#define SOCKETIO_SERVERPP_SCGI_SERVICE_H

#include <boost/asio.hpp>
#include "Netstring.h"
#include "Request.h"
#include <memory>

namespace scgi
{
//namespace Internal
//{
namespace P = std::placeholders;
namespace A = boost::asio;
namespace S = boost::signals2;
using std::string;
using A::ip::tcp;
using boost::system::error_code;
using std::shared_ptr;

template <class PROTOCOL>
class Service
{
    public:
        using CRequest = Request<typename PROTOCOL::socket>;
        using CRequestPtr = shared_ptr<CRequest>;

        typedef PROTOCOL proto;

        void listen(shared_ptr<typename PROTOCOL::acceptor> acceptor)
        {
            m_acceptor = acceptor;
        }
        
        void start_accept()
        {
            auto& io_context = static_cast<boost::asio::io_context&>(m_acceptor->get_executor().context());
            auto request = std::make_shared<CRequest>(io_context);

            m_acceptor->async_accept(request->socket(), std::bind(&Service::onAccept, this, request, std::placeholders::_1));
        }

        S::signal<void (CRequestPtr)> sig_RequestReceived;

    private:

        void onAccept(CRequestPtr request, const error_code& error)
        {
            start_accept();
            if (!error) {
                request->receive(bind([&](CRequestPtr r) { sig_RequestReceived(r);}, request));
            }
        }

    private:
        std::shared_ptr<typename PROTOCOL::acceptor> m_acceptor;
};
//}; // Namespace Internal

//    using Internal::Service;
};

#endif // SOCKETIO_SERVERPP_SCGI_SERVICE_H
