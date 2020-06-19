#ifndef _Shock_h
#define _Shock_h

/* simple Shock interface */

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/ssl.hpp>

#include "AuthToken.h"
#include "parse_url.h"

class Shock
{
    boost::asio::io_context &ioc_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ssl::context &ssl_ctx_;

public:
    Shock(boost::asio::io_context &ioc, boost::asio::ssl::context &ctx)
	: ioc_(ioc)
	, resolver_(boost::asio::make_strand(ioc))
	, ssl_ctx_(ctx) {
    }

    Shock(Shock && s)
	: ioc_(s.ioc_)
	, resolver_(std::move(s.resolver_))
	, ssl_ctx_(s.ssl_ctx_) {
	std::cerr << "shock move construct\n";
    }

    void acl_add_user(const std::string &node_url, const AuthToken &token, boost::asio::yield_context yield) {
	URL url(node_url);
	url.path_append("/acl/all");
	url.query("users=" + token.user());
	std::cerr << url.construct() << "\n";

	if (url.protocol() == "http")
	    request("PUT", url, token, yield);
	else  if (url.protocol() == "https")
	    request_ssl("PUT", url, token, yield);

    }

    void request(const std::string &method, const URL &url, const AuthToken &token, boost::asio::yield_context yield);
    void request_ssl(const std::string &method, const URL &url, const AuthToken &token, boost::asio::yield_context yield);
	  
};


#endif
