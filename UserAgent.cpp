#include "UserAgent.h"

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

void UserAgent::do_request(const std::string &method, const URL &url, const std::string &token, std::map<std::string, std::string> headers,
			   std::function<void(const UserAgent::Response &res)> handle_response, boost::asio::yield_context yield)
{
    beast::tcp_stream stream(net::make_strand(ioc_));
    beast::flat_buffer buffer;
    http::request<http::empty_body> req;
    http::response<http::string_body> res;

    req.method_string(method);
    req.target(url.path_query());
    req.set(http::field::host, url.domain());
    req.set(http::field::user_agent, "p4x-shock");
    if (!token.empty())
	req.set(http::field::authorization, "OAuth " + token);

    for (auto h: headers)
	req.set(h.first, h.second);

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
	handle_response(res);
    } catch (std::exception &e) {
	std::cerr << "Exception processing request result: " << e.what() << "\n";
    }
}

void UserAgent::do_request_ssl(const std::string &method, const URL &url, const std::string &token, std::map<std::string, std::string> headers,
			       std::function<void(const UserAgent::Response &res)> handle_response, boost::asio::yield_context yield)
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
    if (!token.empty())
	req.set(http::field::authorization, "OAuth " + token);

    for (auto h: headers)
	req.set(h.first, h.second);

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
	handle_response(res);
    } catch (std::exception &e) {
	std::cerr << "Exception processing request result: " << e.what() << "\n";
    }
}

