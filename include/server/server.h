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
#include <boost/url.hpp>
#include <boost/url/params_view.hpp>
#include <boost/url/parse.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <taskgroup/taskgroup.h>
#include <utility>
#include <vector>

class Server;

class GetRequest {

  std::vector<std::pair<std::string, std::string>> m_paramsList;
  boost::urls::params_view m_QueryList;

  void setParams(const std::vector<std::pair<std::string, std::string>> &vec) {
    m_paramsList = vec;
  }
  void setQueries(const boost::urls::params_view &querries) {
    m_QueryList = querries;
  }

  void addParams(const std::pair<std::string, std::string> &val) {
    m_paramsList.push_back(val);
  }

public:
  std::optional<std::vector<std::pair<std::string, std::string>>>
  params(std::error_code &ec) const {
    if (m_paramsList.empty()) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return std::nullopt;
    }
    return m_paramsList;
  }

  std::optional<boost::urls::params_view> queries(std::error_code &ec) const {
    if (m_QueryList.empty()) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return std::nullopt;
    }
    return m_QueryList;
  }

  friend class Server;
};

class Server {

  std::optional<boost::urls::params_view> getQueries(const std::string &);

  std::optional<std::vector<std::pair<std::string, std::string>>>
      getParams(std::string, std::string);

  boost::mysql::connection_pool &m_connectionPool;

  using executorType =
      boost::asio::strand<boost::asio::io_context::executor_type>;
  using socketType = typename boost::asio::ip::tcp::socket::rebind_executor<
      executorType>::other;
  using socketStreamType =
      typename boost::beast::tcp_stream::rebind_executor<executorType>::other;
  using aceptorType = typename boost::asio::ip::tcp::acceptor::rebind_executor<
      executorType>::other;

  using returnType = std::optional<
      boost::beast::http::response<boost::beast::http::string_body>>;

  using functionType = boost::asio::awaitable<returnType, executorType>;

  using rgetReqType = boost::asio::awaitable<
      boost::beast::http::response<boost::beast::http::string_body>,
      executorType>;

  using rget =
      std::function<rgetReqType(GetRequest &, boost::mysql::connection_pool &)>;

  std::vector<std::pair<std::string, rget>> m_routes;

  std::vector<std::function<functionType(
      boost::beast::http::request<boost::beast::http::string_body> &,
      boost::mysql::connection_pool &)>>
      m_handleList;
  // handle session is a function that take a string and return's a /*return
  // value*/.

  // boost::asio::awaitable<void, executorType> handleRequest(
  //     boost::beast::http::request<boost::beast::http::string_body> &request)
  //     {

  //   co_return;
  // }

  template <typename Stream>
  boost::asio::awaitable<void, executorType>
  httpsSession(boost::beast::flat_buffer &, Stream &);

  boost::asio::awaitable<void, executorType>
  detechSession(socketStreamType &&, boost::asio::ssl::context &);

public:
  Server(boost::mysql::connection_pool &);

  boost::asio::awaitable<void, executorType>
  startServer(boost::asio::ssl::context &, boost::asio::ip::tcp::endpoint &,
              TaskGroup &);

  void setParams(GetRequest &req,
                 std::vector<std::pair<std::string, std::string>> paramsList);

  void addHandleRequest(
      const std::function<functionType(
          boost::beast::http::request<boost::beast::http::string_body> &,
          boost::mysql::connection_pool &)> &);

  void addHandleRequest(std::string, rget);

  // GetRequest resolvePath(const std::string &path) {
  //   // /users/:id/company/
  // }
}; // namespace Server

inline Server::Server(boost::mysql::connection_pool &connectionPool)
    : m_connectionPool(connectionPool) {};

inline std::optional<boost::urls::params_view>
Server::getQueries(const std::string &target) {
  auto c = boost::urls::parse_relative_ref(target);
  if (c.has_error() || !c->has_query())
    return std::nullopt;
  return c.value().params();
}

inline std::optional<std::vector<std::pair<std::string, std::string>>>
Server::getParams(std::string path, std::string target) {
  if (path.find(':') == std::string::npos)
    return std::nullopt;

  auto paths = boost::urls::parse_relative_ref(path);
  if (paths.has_error())
    return std::nullopt;

  std::vector<std::string> vecPath(paths.value().segments().begin(),
                                   paths.value().segments().end());
  if (vecPath.empty())
    return std::nullopt;
  auto targets = boost::urls::parse_relative_ref(target);
  if (targets.has_error())
    return std::nullopt;

  std::vector<std::string> vecTarget(targets.value().segments().begin(),
                                     targets.value().segments().end());
  if (vecTarget.empty())
    return std::nullopt;

  if (vecTarget.size() != vecPath.size())
    return std::nullopt;

  std::vector<std::pair<std::string, std::string>> list;

  for (int i = 0; i < vecPath.size(); i++) {
    if (auto index = vecPath[i].find(':') != std::string::npos) {
      auto key = vecPath[i].substr(index);
      auto value = vecTarget[i];
      list.push_back(std::make_pair(key, value));
    } else {
      if (vecPath[i] != vecTarget[i])
        return std::nullopt;
    }
  }
  return list;
};

template <typename Stream>
inline boost::asio::awaitable<void, Server::executorType>
Server::httpsSession(boost::beast::flat_buffer &buffer, Stream &socketStream) {

  auto cs = co_await boost::asio::this_coro::cancellation_state;

  while (!cs.cancelled()) {
    boost::beast::http::request_parser<boost::beast::http::string_body>
        reqParser;
    reqParser.body_limit(10000);

    boost::system::error_code ec;

    auto readSize = co_await boost::beast::http::async_read(
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

    // for (auto &c : m_handleList) {
    //   auto resOptional = co_await c(reqParser.get(), m_connectionPool);

    //   if (resOptional.has_value()) {
    //     keepAlive = resOptional.value().keep_alive();
    //     auto writeSize = co_await boost::beast::http::async_write(
    //         socketStream, resOptional.value(),
    //         boost::asio::redirect_error(ec));

    //     break;
    //   }
    // }

    for (auto &c : m_routes) {
      auto path = std::get<0>(c);
      auto func = std::get<1>(c);

      // std::string target = reqParser.get().target();
      auto targetHasQueries = getQueries(reqParser.get().target());
      auto target =
          boost::urls::parse_relative_ref(reqParser.get().target())->path();
      auto hasParams = getParams(path, target);

      std::cout << "path: " << target << std::endl;

      GetRequest req;

      if (targetHasQueries.has_value()) {

        req.setQueries(targetHasQueries.value());

        // auto target =
        //     boost::urls::parse_relative_ref(reqParser.get().target())->path();
        // auto hasParams = getParams(path, target);

        if (hasParams.has_value()) {
          req.setParams(hasParams.value());
          auto res = co_await func(req, m_connectionPool);

          keepAlive = res.keep_alive();
          auto writeSize = co_await boost::beast::http::async_write(
              socketStream, res, boost::asio::redirect_error(ec));
          break;
        }
      }
      if (path == target) {
        auto res = co_await func(req, m_connectionPool);

        keepAlive = res.keep_alive();
        auto writeSize = co_await boost::beast::http::async_write(
            socketStream, res, boost::asio::redirect_error(ec));
        break;
      }
    }

    if (!keepAlive)
      break;
  }
  co_return;
}

inline boost::asio::awaitable<void, Server::executorType>
Server::startServer(boost::asio::ssl::context &ctx,
                    boost::asio::ip::tcp::endpoint &endPoint,
                    TaskGroup &taskGroup) {
  auto cs = co_await boost::asio::this_coro::cancellation_state;
  // get the context
  auto executor = co_await boost::asio::this_coro::executor;

  co_await boost::asio::this_coro::reset_cancellation_state(
      boost::asio::enable_total_cancellation());

  auto acceptor = aceptorType{executor, endPoint};

  boost::system::error_code ec;

  while (!cs.cancelled()) {
    auto strand = boost::asio::make_strand(executor.get_inner_executor());

    auto socket =
        co_await acceptor.async_accept(strand, boost::asio::redirect_error(ec));

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
              std::cerr << "co_spawn error from detect session..." << std::endl;
              return;
            }
          }
        }));
  }
  co_return;
}

inline boost::asio::awaitable<void, Server::executorType>
Server::detechSession(socketStreamType &&socket,
                      boost::asio::ssl::context &ctx) {

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

      co_await socketSslStream.async_shutdown(boost::asio::redirect_error(ec));
      if (ec && ec != boost::asio::ssl::error::stream_truncated) {
        throw boost::system::system_error(ec);
      }
    }
  }
  // co_await httpsSession(flatBuffer, socketStream);
}

inline void Server::addHandleRequest(
    const std::function<functionType(
        boost::beast::http::request<boost::beast::http::string_body> &,
        boost::mysql::connection_pool &)> &func) {
  m_handleList.push_back(std::move(func));
}

inline void Server::addHandleRequest(std::string path, rget req) {
  m_routes.push_back(
      std::make_pair<std::string, rget>(std::move(path), std::move(req)));
}