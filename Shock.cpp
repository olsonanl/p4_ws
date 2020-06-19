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
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
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

    try {
	auto doc = boost::json::parse(res.body());
	auto obj = doc.as_object();
	auto status = obj["status"].as_int64();
	if (status != 200)
	{
	    auto error = obj["error"];
	    std::cerr << "error on request: " << error << "\n";
	}
    } catch (std::exception &e) {
	std::cerr << "Exception processing request result: " << e.what() << "\n";
    }
}

void Shock::request_ssl(const std::string &method, const URL &url, const AuthToken &token, boost::asio::yield_context yield)
{
    beast::ssl_stream<beast::tcp_stream> stream(net::make_strand(ioc_), ssl_ctx_);
    beast::flat_buffer buffer;
    http::request<http::empty_body> req;
    http::response<http::string_body> res;

    if (!SSL_set_tlsext_host_name(stream.native_handle(), url.domain().c_str()))
    {
	beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
	std::cerr << ec.message() << "\n";
	return;
    }

    req.method_string(method);
    req.target(url.path_query());
    req.set(http::field::host, url.domain());
    req.set(http::field::user_agent, "p4x-shock");
    req.set(http::field::authorization, "OAuth " + token.token());

    boost::system::error_code ec;
    auto res_iter = resolver_.async_resolve(url.domain(), url.port(), yield[ec]);
    if (ec)
	fail(ec, "resolve");

    beast::get_lowest_layer(stream).async_connect(res_iter, yield[ec]);
    if (ec)
    {
	fail(ec, "connect");
    }

    stream.async_handshake(ssl::stream_base::client, yield[ec]);
    if (ec)
    {
	fail(ec, "handshake");
    }

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

    try {
	auto doc = boost::json::parse(res.body());
	auto obj = doc.as_object();
	auto status = obj["status"].as_int64();
	if (status != 200)
	{
	    auto error = obj["error"];
	    std::cerr << "error on request: " << error << "\n";
	    std::cerr << req << "\n";
	}
    } catch (std::exception &e) {
	std::cerr << "Exception processing request result: " << e.what() << "\n";
    }
}

