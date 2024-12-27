#pragma once

#include <beauty/export.hpp>
#include <beauty/route.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http/field.hpp>
#include <exception>
#include <optional>
#include <unordered_map>

namespace beast = boost::beast;

namespace beauty {
// --------------------------------------------------------------------------
class BEAUTY_EXPORT router {
public:
    using routes = std::unordered_map<beast::http::verb, std::vector<route>>;

    void add_route(beast::http::verb v, route&& r);

    routes::const_iterator find(beast::http::verb v) const noexcept {
        return _routes.find(v);
    }

    routes::const_iterator begin() const noexcept {
        return _routes.begin();
    }

    routes::const_iterator end() const noexcept {
        return _routes.end();
    }

    std::optional<route_cb> post_routing_handler;
    std::unordered_map<http::field, std::string> default_headers;
    std::optional<std::function<void(const beauty::request& req, beauty::response& res, std::exception_ptr exc_ptr)>>
        exception_handler;

private:
    routes _routes;
};

}  // namespace beauty
