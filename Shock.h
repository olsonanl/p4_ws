#ifndef _Shock_h
#define _Shock_h

/* simple Shock interface */

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include "AuthToken.h"
#include "parse_url.h"

class Shock
{
    boost::asio::io_context &ioc_;
    boost::asio::ip::tcp::resolver resolver_;

public:
    Shock(boost::asio::io_context &ioc)
	: ioc_(ioc)
	, resolver_(boost::asio::make_strand(ioc)) {
    }

    void acl_add_user(const std::string &node_url, const AuthToken &token, boost::asio::yield_context yield) {
	URL url(node_url);
	url.path_append("/acl/all");
	url.query("users=" + token.user());
	std::cerr << url.construct() << "\n";

	request("PUT", url, token, yield);
    }

    void request(const std::string &method, const URL &url, const AuthToken &token, boost::asio::yield_context yield);
	  
};


#endif
