#pragma once

#include <beauty/exception.hpp>
#include <beauty/router.hpp>
#include <beauty/utils.hpp>
#include <beauty/version.hpp>
#include <beauty/websocket_session.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <exception>

#if BEAUTY_ENABLE_OPENSSL
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>
#endif

#include <memory>
#include <string>
#include <type_traits>

namespace asio = boost::asio;
namespace beast = boost::beast;

namespace beauty {

// --------------------------------------------------------------------------
// Handles an HTTP/S server connection
//---------------------------------------------------------------------------
template <bool SSL>
class session : public std::enable_shared_from_this<session<SSL>> {
public:
    using stream_type = std::conditional_t<SSL, asio::ssl::stream<asio::ip::tcp::socket&>, void*>;

public:
    template <bool U = SSL, typename std::enable_if_t<!U, int> = 0>
    session(asio::io_context& ioc, asio::ip::tcp::socket&& socket, const beauty::router& router)
            : _socket(std::move(socket)), _strand(asio::make_strand(ioc)), _router(router) {}

#if BEAUTY_ENABLE_OPENSSL
    template <bool U = SSL, typename std::enable_if_t<U, int> = 0>
    session(
        asio::io_context& ioc, asio::ip::tcp::socket&& socket, const beauty::router& router, asio::ssl::context& ctx
    )
            : _socket(std::move(socket)), _stream(_socket, ctx), _strand(asio::make_strand(ioc)), _router(router) {}
#endif

    // Start the asynchronous operation
    void run() {
        if constexpr (SSL) {
#if BEAUTY_ENABLE_OPENSSL
            // Perform the SSL handshake
            _stream.async_handshake(
                asio::ssl::stream_base::server,
                asio::bind_executor(_strand, [me = this->shared_from_this()](auto ec) { me->on_ssl_handshake(ec); })
            );
#endif
        } else {
            do_read();
        }
    }

    void on_ssl_handshake(boost::system::error_code ec) {
        if (ec) {
            return fail(ec, "failed handshake");
        }

        do_read();
    }

    void do_read() {
        // std::cout << "session: do read" << std::endl;
        //  Make a new request_parser before reading
        _request_parser = std::make_unique<beast::http::request_parser<beast::http::string_body>>();
        _request_parser->body_limit(1024 * 1024 * 1024);  // 1Go..

        // Read a full request (only if on _stream/_socket)
        if constexpr (SSL) {
            beast::http::async_read(
                _stream, _buffer, *_request_parser,
                asio::bind_executor(
                    _strand, [me = this->shared_from_this(
                              )](auto ec, auto bytes_transferred) { me->on_read(ec, bytes_transferred); }
                )
            );
        } else {
            beast::http::async_read(
                _socket, _buffer, *_request_parser,
                asio::bind_executor(
                    _strand, [me = this->shared_from_this(
                              )](auto ec, auto bytes_transferred) { me->on_read(ec, bytes_transferred); }
                )
            );
        }
    }

    void on_read(boost::system::error_code ec, std::size_t /* bytes_transferred */) {
        // std::cout << "session: on read" << std::endl;

        // This means they closed the connection
        if (ec == beast::http::error::end_of_stream) {
            return do_close();
        }

        if (ec) {
            return fail(ec, "read");
        }

        // Send the response
        auto response = handle_request();

        if (response) {  // Probably not a WebSocket request
            if (!response->is_postponed()) {
                do_write(response);
            } else {
                response->on_done([me = this->shared_from_this(), response] { me->do_write(response); });
            }
        }
    }

    void do_write(const std::shared_ptr<response>& response) {
        // std::cout << "session: do write" << std::endl;
        response->prepare_payload();

        // Write the response
        if constexpr (SSL) {
            beast::http::async_write(
                this->_stream, *response,
                asio::bind_executor(
                    this->_strand,
                    [me = this->shared_from_this(), response](auto ec, auto bytes_transferred) {
                        me->on_write(ec, bytes_transferred, response->need_eof());
                    }
                )
            );
        } else {
            beast::http::async_write(
                this->_socket, *response,
                asio::bind_executor(
                    this->_strand,
                    [me = this->shared_from_this(), response](auto ec, auto bytes_transferred) {
                        me->on_write(ec, bytes_transferred, response->need_eof());
                    }
                )
            );
        }
    }

    void on_write(boost::system::error_code ec, std::size_t /* bytes_transferred */, bool close) {
        // std::cout << "session: do write" << std::endl;
        if (ec) {
            return fail(ec, "write");
        }

        if (close) {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return do_close();
        }

        // Read another request
        // std::cout << "session: Read another request" << std::endl;
        // Allow staying alive the session in case of postponed response
        do_read();
    }

    void do_close() {
        // std::cout << "session: do close, Shutdown the connection" << std::endl;
        if constexpr (SSL) {
            // Perform the SSL shutdown
            _stream.async_shutdown(asio::bind_executor(_strand, [me = this->shared_from_this()](auto ec) {
                me->on_ssl_shutdown(ec);
            }));
        } else {
            // Send a TCP shutdown
            boost::system::error_code ec;
            _socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
            _socket.close();
        }
    }

    void on_ssl_shutdown(boost::system::error_code ec) {
        if (ec) {
            return fail(ec, "shutdown");
        }

        if constexpr (SSL) {
            _stream.lowest_layer().close();
        }
    }

private:
    asio::ip::tcp::socket _socket;
    stream_type _stream = {};
    asio::strand<asio::io_context::executor_type> _strand;
    beast::flat_buffer _buffer;
    beauty::request _request;
    std::unique_ptr<beast::http::request_parser<beast::http::string_body>> _request_parser;
    bool _is_websocket = false;

    const beauty::router& _router;

private:
    std::shared_ptr<response> handle_request() {
        // Make sure we can handle the method
        _request = _request_parser->release();
        _request.remote(_socket.remote_endpoint());
        _request.timestamp = std::chrono::high_resolution_clock::now();

        _is_websocket = (beast::websocket::is_upgrade(_request));
        // std::cout << "session: handle " << (_is_websocket ? "websocket" :
        // "request") << ", method: " << _request.method_string() << ", target: " <<
        // _request.target() << std::endl;

        auto found_method = _router.find(_request.method());
        if (found_method == _router.end()) {
            return helper::bad_request(_request, "Not supported HTTP-method");
        }

        // Try to match a route for this request target
        for (auto&& route : found_method->second) {
            if (route.match(_request, _is_websocket)) {
                // Match will update parameters request from the URL
                try {
                    if (_is_websocket) {
                        // Create a websocket session, and transferring ownership
                        std::make_shared<websocket_session>(std::move(_socket), route)->run(_request);
                        return nullptr;
                    } else {
                        auto res = std::make_shared<response>(beast::http::status::ok, _request.version());
                        res->set(beast::http::field::server, BEAUTY_PROJECT_VERSION);
                        res->keep_alive(_request.keep_alive());

                        route.execute(_request, *res);  // Call the route user handler
                        for (const auto& [key, value] : _router.default_headers) {
                            res->set_header(key, value);
                        }
                        if (_router.post_routing_handler) {
                            _router.post_routing_handler.value()(_request, *res);
                        }
                        return res;
                    }
                } catch (const beauty::exception& ex) {
                    auto res = ex.create_response(_request);
                    for (const auto& [key, value] : _router.default_headers) {
                        res->set_header(key, value);
                    }
                    if (_router.post_routing_handler) {
                        _router.post_routing_handler.value()(_request, *res);
                    }
                    return res;
                } catch (...) {
                    if (_router.exception_handler) {
                        auto res =
                            std::make_shared<response>(beast::http::status::internal_server_error, _request.version());
                        res->set(beast::http::field::server, BEAUTY_PROJECT_VERSION);
                        res->keep_alive(_request.keep_alive());
                        _router.exception_handler.value()(_request, *res, std::current_exception());
                        for (const auto& [key, value] : _router.default_headers) {
                            res->set_header(key, value);
                        }
                        if (_router.post_routing_handler) {
                            _router.post_routing_handler.value()(_request, *res);
                        }
                        return res;
                    } else {
                        try {
                            std::rethrow_exception(std::current_exception());
                        } catch (const std::exception& ex) {
                            auto res = helper::server_error(_request, ex.what());
                            for (const auto& [key, value] : _router.default_headers) {
                                res->set_header(key, value);
                            }
                            if (_router.post_routing_handler) {
                                _router.post_routing_handler.value()(_request, *res);
                            }
                            return res;
                        }
                    }
                }
            }
        }

        auto res = helper::not_found(_request);
        for (const auto& [key, value] : _router.default_headers) {
            res->set_header(key, value);
        }
        if (_router.post_routing_handler) {
            _router.post_routing_handler.value()(_request, *res);
        }
        return res;
    }
};

// --------------------------------------------------------------------------
using session_http = session<false>;

#if BEAUTY_ENABLE_OPENSSL
using session_https = session<true>;
#endif

}  // namespace beauty
