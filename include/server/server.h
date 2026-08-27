#pragma once

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core/buffers_generator.hpp>
#include <boost/beast/core/detect_ssl.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/impl/error.hpp>
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/impl/write.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/parser_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/mysql.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/none.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <taskgroup/taskgroup.h>
#include <vector>

class Server {

  boost::mysql::connection_pool &m_connectionPool;

  using executorType =
      boost::asio::strand<boost::asio::io_context::executor_type>;
  using socketType = typename boost::asio::ip::tcp::socket::rebind_executor<
      executorType>::other;
  using socketStreamType =
      typename boost::beast::tcp_stream::rebind_executor<executorType>::other;
  using aceptorType = typename boost::asio::ip::tcp::acceptor::rebind_executor<
      executorType>::other;

  std::vector<std::function<std::optional<
      boost::beast::http::response<boost::beast::http::string_body>>(
      boost::beast::http::request<boost::beast::http::empty_body> &,
      boost::mysql::connection_pool &)>>
      m_handleList;
  // handle session is a function that take a string and return's a /*return
  // value*/.

  boost::asio::awaitable<void, executorType> handleRequest(
      boost::beast::http::request<boost::beast::http::empty_body> &request) {

    co_return;
  }

  template <typename Stream>
  boost::asio::awaitable<void, executorType>
  httpsSession(boost::beast::flat_buffer &buffer, Stream &socketStream) {

    auto cs = co_await boost::asio::this_coro::cancellation_state;

    while (!cs.cancelled()) {
      boost::beast::http::request_parser<boost::beast::http::empty_body>
          reqParser;
      reqParser.body_limit(10000);

      boost::system::error_code ec;

      auto readSize = co_await boost::beast::http::async_read_header(
          socketStream, buffer, reqParser, boost::asio::redirect_error(ec));

      if (ec == boost::beast::http::error::end_of_stream ||
          ec == boost::beast::error::timeout)
        co_return;

      buffer.consume(readSize);

      if (ec) {
        std::cerr << ec.what() << std::endl;
        co_return;
      }
      bool keepAlive = false;

      for (int i = 0; i < m_handleList.size(); i++) {
        auto resOptional = m_handleList[i](reqParser.get(), m_connectionPool);

        if (resOptional.has_value()) {
          keepAlive = resOptional.value().keep_alive();
          auto writeSize = co_await boost::beast::http::async_write(
              socketStream, resOptional.value(),
              boost::asio::redirect_error(ec));

          break;
        }
      }

      if (!keepAlive)
        break;
    }
    co_return;
  }

  boost::asio::awaitable<void, executorType>
  detechSession(socketStreamType &&socket, boost::asio::ssl::context &ctx) {

    co_await boost::asio::this_coro::reset_cancellation_state(
        boost::asio::enable_total_cancellation(),
        boost::asio::enable_terminal_cancellation());

    co_await boost::asio::this_coro::throw_if_cancelled(false);

    boost::beast::flat_buffer flatBuffer;
    socketStreamType socketStream{std::move(socket)};

    socketStream.expires_after(std::chrono::seconds(60));

    if (co_await boost::beast::async_detect_ssl(socketStream, flatBuffer)) {

      boost::asio::ssl::stream<socketStreamType> socketSslStream(
          std::move(socketStream), ctx);

      auto size = co_await socketSslStream.async_handshake(
          boost::asio::ssl::stream_base::handshake_type::server,
          flatBuffer.data());

      flatBuffer.consume(size);
      // start session.
      co_await httpsSession(flatBuffer, socketSslStream);

      if (socketSslStream.lowest_layer().is_open()) {
        boost::system::error_code ec;

        co_await socketSslStream.async_shutdown(
            boost::asio::redirect_error(ec));
        if (ec && ec != boost::asio::ssl::error::stream_truncated) {
          throw boost::system::system_error(ec);
        }
      }
    }
    // co_await httpsSession(flatBuffer, socketStream);
  }

public:
  Server(boost::mysql::connection_pool &connectionPool)
      : m_connectionPool(connectionPool) {};

  boost::asio::awaitable<void, executorType>
  startServer(boost::asio::ssl::context &ctx,
              boost::asio::ip::tcp::endpoint &endPoint, TaskGroup &taskGroup) {
    auto cs = co_await boost::asio::this_coro::cancellation_state;
    // get the context
    auto executor = co_await boost::asio::this_coro::executor;

    co_await boost::asio::this_coro::reset_cancellation_state(
        boost::asio::enable_total_cancellation());

    auto acceptor = aceptorType{executor, endPoint};

    boost::system::error_code ec;

    while (!cs.cancelled()) {
      auto strand = boost::asio::make_strand(executor.get_inner_executor());

      auto socket = co_await acceptor.async_accept(
          strand, boost::asio::redirect_error(ec));

      if (ec) {
        if (ec == boost::asio::error::operation_aborted)
          co_return;
        std::cerr << "acceptor error" << std::endl;
        co_return;
      }

      boost::asio::co_spawn(
          std::move(strand),
          detechSession(socketStreamType{std::move(socket)}, ctx),
          taskGroup.adapt([](std::exception_ptr e) {
            if (e) {
              try {
                std::rethrow_exception(e);
              } catch (const std::exception &ec) {
                std::cerr << ec.what() << std::endl;
                std::cerr << "co_spawn error from detect session..."
                          << std::endl;
                return;
              }
            }
          }));
    }
    co_return;
  }

  void addHandleRequest(
      const std::function<std::optional<
          boost::beast::http::response<boost::beast::http::string_body>>(
          boost::beast::http::request<boost::beast::http::empty_body> &,
          boost::mysql::connection_pool &)> &func) {
    m_handleList.push_back(std::move(func));
  }
}; // namespace Server