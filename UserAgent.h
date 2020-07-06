#ifndef _UserAgent_h
#define _UserAgent_h

/* simple UserAgent interface */

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>
#include <functional>

#include "parse_url.h"

class UserAgent
{
    boost::asio::io_context &ioc_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ssl::context &ssl_ctx_;

public:

    typedef boost::beast::http::response<boost::beast::http::string_body> Response;
    
    UserAgent(boost::asio::io_context &ioc, boost::asio::ssl::context &ctx)
	: ioc_(ioc)
	, resolver_(boost::asio::make_strand(ioc))
	, ssl_ctx_(ctx) {
    }

    UserAgent(UserAgent && s)
	: ioc_(s.ioc_)
	, resolver_(std::move(s.resolver_))
	, ssl_ctx_(s.ssl_ctx_) {
    }

    void request(const std::string &method, const URL &url, const std::string &token, std::map<std::string, std::string> headers,
		 std::function<void(const Response &res)> handle_response, boost::asio::yield_context yield)
    {
	if (url.protocol() == "http")
	    do_request(method, url, token, headers, handle_response, yield);
	else
	    do_request_ssl(method, url, token, headers, handle_response, yield);
    }
    
		
    void request(const std::string &method, const URL &url, std::map<std::string, std::string> headers,
		 std::function<void(const Response &res)> handle_response, boost::asio::yield_context yield)
    {
	if (url.protocol() == "http")
	    do_request(method, url, "", headers, handle_response, yield);
	else
	    do_request_ssl(method, url, "", headers, handle_response, yield);
    }

private:
    
    void do_request_ssl(const std::string &method, const URL &url, const std::string & token, std::map<std::string, std::string> headers,
			std::function<void(const Response &res)> handle_response, boost::asio::yield_context yield);
    void do_request(const std::string &method, const URL &url, const std::string &token, std::map<std::string, std::string> headers,
		    std::function<void(const Response &res)> handle_response, boost::asio::yield_context yield);
	  
};


#endif
