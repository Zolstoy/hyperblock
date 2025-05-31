#include <fmt/color.h>
#include <gtest/gtest.h>
#include <spdlog/common.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <chrono>
#include <expected>
#include <format>
#include <memory>
#include <sstream>
#include <thread>

#include "../instance.hpp"
#include "boost/asio/ssl/stream.hpp"
#include "common.hpp"

using namespace boost;

TEST(Instance, InstanciationNominal)
{
    asio::io_context ioc;
    ASSERT_NO_THROW(hyper_block::instance(ioc, 0, CERT, KEY));
}

TEST(Instance, RunAsyncError)
{
    asio::io_context ioc;
    {
        hyper_block::instance instance(ioc, 2456, "", KEY);
        ASSERT_FALSE(instance.run_async());
    }
    {
        hyper_block::instance instance(ioc, 2456, CERT, "");
        ASSERT_FALSE(instance.run_async());
    }
    {
        hyper_block::instance instance(ioc, 2456, "", "");
        ASSERT_FALSE(instance.run_async());
    }
}

TEST(Instance, RunAsyncNominal)
{
    asio::io_context      ioc;
    hyper_block::instance instance(ioc, 2456, CERT, KEY);
    ASSERT_TRUE(instance.run_async());
    ASSERT_EQ(hyper_block::error::instance_already_running, instance.run_async().error());
}

struct expected {
    struct no_error {
    };
    struct bad_event_type {
    };
};

#define HYPERBLOCK_TESTS_PRELUDE(code)                                                                              \
    asio::io_context      ioc_server;                                                                               \
    hyper_block::instance instance(ioc_server, 2456, CERT, KEY, [](hyper_block::step::event &&event) -> bool code); \
    auto                  port  = instance.run_async().value();                                                     \
    auto                  step1 = std::async(std::launch::async, [&]() { return ioc_server.run(); });               \
    asio::io_context      ioc_client;                                                                               \
    asio::ssl::context    ssl_context(asio::ssl::context::sslv23);                                                  \
    ssl_context.set_verify_mode(asio::ssl::verify_none);                                                            \
    ssl_context.add_certificate_authority(asio::buffer(CA_CERT.data(), CA_CERT.size()));                            \
    asio::ip::tcp::resolver                  resolver(ioc_client);                                                  \
    asio::ip::tcp::resolver::results_type    endpoints = resolver.resolve("localhost", "2456");                     \
    asio::ssl::stream<asio::ip::tcp::socket> socket(ioc_client, ssl_context)

TEST(Instance, Accept)
{
    HYPERBLOCK_TESTS_PRELUDE({
        if (event.index() != hyper_block::step::events::server::accept::value)
            throw expected::bad_event_type{};
        throw expected::no_error{};
    });

    asio::connect(socket.next_layer(), endpoints);
    ASSERT_THROW(step1.get(), expected::no_error);
}

TEST(Instance, Handshake)
{
    HYPERBLOCK_TESTS_PRELUDE({
        if (event.index() == hyper_block::step::events::server::accept::value)
            return true;
        if (event.index() != hyper_block::step::events::session::handshake::value)
            throw expected::bad_event_type{};
        throw expected::no_error{};
    });

    asio::connect(socket.next_layer(), endpoints);
    socket.handshake(asio::ssl::stream_base::client);
    ASSERT_THROW(step1.get(), expected::no_error);
}

TEST(Instance, Upgrade)
{
    HYPERBLOCK_TESTS_PRELUDE({
        if (event.index() == hyper_block::step::events::server::accept::value)
            return true;
        if (event.index() == hyper_block::step::events::session::handshake::value)
            return true;
        if (event.index() != hyper_block::step::events::session::accept::value)
            throw expected::bad_event_type{};
        throw expected::no_error{};
    });

    asio::connect(socket.next_layer(), endpoints);
    socket.handshake(asio::ssl::stream_base::client);
    boost::beast::websocket::stream<asio::ssl::stream<asio::ip::tcp::socket>> ws(std::move(socket));

    ws.accept();

    ASSERT_THROW(step1.get(), expected::no_error);
}

namespace hyper_block {
namespace tests {

class elapsed_formatter_flag : public spdlog::custom_flag_formatter
{
   private:
    std::chrono::steady_clock::time_point start_;

   public:
    elapsed_formatter_flag(std::chrono::steady_clock::time_point start)
        : start_(start)
    {}

   public:
    void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override
    {
        auto                         now     = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = now - start_;
        std::string some_txt                 = std::format("\033[38;2;100;20;200m{:10.3f}\033[39;49m", elapsed.count());
        dest.append(some_txt.data(), some_txt.data() + some_txt.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<elapsed_formatter_flag>(start_);
    }
};

class thread_formatter_flag : public spdlog::custom_flag_formatter
{
   public:
    void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override
    {
        auto              thread_id = std::this_thread::get_id();
        std::stringstream ss;
        ss << thread_id;
        std::string some_txt = std::format("\033[38;2;20;100;200m{:5}\033[39;49m", ss.str());
        dest.append(some_txt.data(), some_txt.data() + some_txt.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<thread_formatter_flag>();
    }
};

}   // namespace tests
}   // namespace hyper_block

int
main(int argc, char **argv)
{
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    auto now       = std::chrono::steady_clock::now();
    formatter->add_flag<hyper_block::tests::elapsed_formatter_flag>('*', now);
    formatter->add_flag<hyper_block::tests::thread_formatter_flag>('+');
    formatter->set_pattern("[%*][%+] %v");
    auto logger = spdlog::stdout_color_mt("console", spdlog::color_mode::always);
    logger->set_level(spdlog::level::trace);
    logger->set_formatter(std::move(formatter));

    spdlog::set_default_logger(logger);

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
