#pragma once

#include <beauty/acceptor.hpp>
#include <beauty/application.hpp>
#include <beauty/endpoint.hpp>
#include <beauty/export.hpp>
#include <beauty/route.hpp>
#include <beauty/router.hpp>
#include <beauty/swagger.hpp>
#include <string>
#include <unordered_map>

namespace beauty {
// --------------------------------------------------------------------------
class BEAUTY_EXPORT server {
public:
    // Avoid PATH duplication when adding multiple verbs to the same route (PATH)
    // First step before refactoring to make the route the key for the router
    class server_route {
    public:
        server_route(server& s, std::string path) : _server(s), _path(std::move(path)) {}

        // Http verbs
        server_route& get(route_cb&& cb) {
            _server.get(_path, std::move(cb));
            return *this;
        };

        server_route& get(const route_info& route_info, route_cb&& cb) {
            _server.get(_path, route_info, std::move(cb));
            return *this;
        }

        server_route& put(route_cb&& cb) {
            _server.put(_path, std::move(cb));
            return *this;
        };

        server_route& put(const route_info& route_info, route_cb&& cb) {
            _server.put(_path, route_info, std::move(cb));
            return *this;
        }

        server_route& post(route_cb&& cb) {
            _server.post(_path, std::move(cb));
            return *this;
        };

        server_route& post(const route_info& route_info, route_cb&& cb) {
            _server.post(_path, route_info, std::move(cb));
            return *this;
        }

        server_route& patch(route_cb&& cb) {
            _server.patch(_path, std::move(cb));
            return *this;
        };

        server_route& patch(const route_info& route_info, route_cb&& cb) {
            _server.patch(_path, route_info, std::move(cb));
            return *this;
        }

        server_route& options(route_cb&& cb) {
            _server.options(_path, std::move(cb));
            return *this;
        };

        server_route& options(const route_info& route_info, route_cb&& cb) {
            _server.options(_path, route_info, std::move(cb));
            return *this;
        }

        server_route& del(route_cb&& cb) {
            _server.del(_path, std::move(cb));
            return *this;
        };

        server_route& del(const route_info& route_info, route_cb&& cb) {
            _server.del(_path, route_info, std::move(cb));
            return *this;
        }

        // Websocket
        server_route& ws(ws_handler&& handler) {
            _server.ws(_path, std::move(handler));
            return *this;
        }

    private:
        server& _server;
        std::string _path;
    };

public:
    server();
    explicit server(beauty::application& app);
#if BEAUTY_ENABLE_OPENSSL
    explicit server(certificates&& c) : _app(beauty::application::Instance(std::move(c))) {}
#endif
    ~server();

    server(const server&) = delete;
    server& operator=(const server&) = delete;

    server(server&&) = delete;
    server& operator=(server&&) = delete;

    server& concurrency(int concurrency) {
        _concurrency = concurrency;
        return *this;
    }

    server_route add_route(const std::string& path) {
        return { *this, path };
    }

    // Legacy API, should not be used anymore to avoid PATH duplication
    server& get(const std::string& path, route_cb&& cb);
    server& get(const std::string& path, const route_info& route_info, route_cb&& cb);
    server& put(const std::string& path, route_cb&& cb);
    server& put(const std::string& path, const route_info& route_info, route_cb&& cb);
    server& post(const std::string& path, route_cb&& cb);
    server& post(const std::string& path, const route_info& route_info, route_cb&& cb);
    server& patch(const std::string& path, route_cb&& cb);
    server& patch(const std::string& path, const route_info& route_info, route_cb&& cb);
    server& options(const std::string& path, route_cb&& cb);
    server& options(const std::string& path, const route_info& route_info, route_cb&& cb);
    server& del(const std::string& path, route_cb&& cb);
    server& del(const std::string& path, const route_info& route_info, route_cb&& cb);

    server& ws(const std::string& path, ws_handler&& handler);

    void listen(int port = 0, const std::string& address = "0.0.0.0");
    void stop();
    void run();
    void wait();

    const beauty::endpoint& endpoint() const {
        return _endpoint;
    }

    int port() const {
        return endpoint().port();
    }

    const beauty::router& router() const noexcept {
        return _router;
    }

    const beauty::server_info& info() const noexcept {
        return _server_info;
    }

    void info(const beauty::server_info& info) {
        _server_info = info;
    }

    void enable_swagger(const char* swagger_entrypoint = "/swagger");
    void set_default_headers(const std::unordered_map<http::field, std::string>& default_headers);
    void set_post_routing_handler(route_cb&& cb);
    void set_exception_handler(
        const std::function<void(const beauty::request& req, beauty::response& res, std::exception_ptr exc_ptr)>& cb
    );

private:
    beauty::application& _app;
    int _concurrency{ 1 };
    beauty::router _router;
    std::shared_ptr<beauty::acceptor> _acceptor;

    beauty::endpoint _endpoint;
    beauty::server_info _server_info;
};

}  // namespace beauty
