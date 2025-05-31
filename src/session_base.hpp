#pragma once

#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cereal/archives/json.hpp>
#include <cstdlib>
#include <memory>
#include <type_traits>

#include "boost/beast/core/error.hpp"
#include "protocol.hpp"
#include "step.hpp"

namespace hyper_block {

template <bool is_secure, typename T>
class session_base : public std::enable_shared_from_this<T>
{
   protected:
    using plain_stream = boost::beast::tcp_stream;
    using tls_stream   = boost::asio::ssl::stream<boost::beast::tcp_stream>;

    using plain_websocket = boost::beast::websocket::stream<plain_stream>;
    using tls_websocket   = boost::beast::websocket::stream<tls_stream>;

    using websocket = std::conditional_t<is_secure, tls_websocket, plain_websocket>;

   protected:
    boost::beast::flat_buffer      buffer_;
    websocket                      ws_;
    std::function<step::callback> &step_callback_;

   protected:
    session_base(websocket &&websocket, std::function<step::callback> &step_callback)
        : ws_(std::move(websocket))
        , step_callback_(step_callback)
    {}

    virtual ~session_base() = default;

   public:
    virtual void run_async() = 0;

   public:
    void on_accept(boost::beast::error_code ec)
    {
        HYPERBLOCK_STEP(session::accept{});
        std::cout << "WebSocket connection accepted" << std::endl;
        do_read();
    }

    void do_read()
    {
        ws_.async_read(buffer_, std::bind(&session_base<is_secure, T>::on_read, this->shared_from_this(),
                                          std::placeholders::_1, std::placeholders::_2));
    }

    void on_read(boost::beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec == boost::beast::websocket::error::closed)
            return;

        if (ec)
        {
            std::cerr << "Read failed: " << ec.message() << std::endl;
            return;
        }

        std::string message = boost::beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());
        std::cout << "Received message: " << message << std::endl;

        std::stringstream        ss(message);
        cereal::JSONInputArchive iarchive(ss);

        hyper_block::protocol::authentication auth;

        iarchive(auth);

        std::cout << "Nickname: " << auth.nickname << std::endl;
        std::cout << "Password: " << auth.password << std::endl;
    };
};

}   // namespace hyper_block
