#include "Shock.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <boost/json.hpp>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

// Report a failure
static void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

void Shock::request(const std::string &method, const URL &url, const AuthToken &token, boost::asio::yield_context yield)
{
    beast::tcp_stream stream(net::make_strand(ioc_));
    beast::flat_buffer buffer;
    http::request<http::empty_body> req;
    http::response<http::string_body> res;

    req.method_string(method);
    req.target(url.path_query());
    req.set(http::field::host, url.domain());
    req.set(http::field::user_agent, "p4x-shock");
    req.set(http::field::authorization, "OAuth " + token.token());

    boost::system::error_code ec;
    auto res_iter = resolver_.async_resolve(url.domain(), url.port(), yield[ec]);
    if (ec)
	fail(ec, "resolve");

    stream.async_connect(res_iter, yield[ec]);
    if (ec)
    {
	fail(ec, "connect");
    }

    std::cerr << req;
    http::async_write(stream, req, yield[ec]);
    
    if (ec)
    {
	fail(ec, "write");
    }

    http::async_read(stream, buffer, res, yield[ec]);
    if (ec)
    {
	fail(ec, "read");
    }
    std::cerr << res << "\n";

    auto doc = boost::json::parse(res.body());
    std::cerr << boost::json::to_string(doc) << "\n";
}
