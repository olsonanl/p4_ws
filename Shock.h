#ifndef _Shock_h
#define _Shock_h

/* simple Shock interface */

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/json.hpp>

#include "AuthToken.h"
#include "parse_url.h"
#include "WorkspaceErrors.h"

class Shock
{
    boost::asio::io_context &ioc_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ssl::context &ssl_ctx_;
    std::string default_server_;
public:
    Shock(boost::asio::io_context &ioc, boost::asio::ssl::context &ctx, std::string default_server)
	: ioc_(ioc)
	, resolver_(boost::asio::make_strand(ioc))
	, ssl_ctx_(ctx)
	, default_server_(default_server) {
    }

    Shock(Shock && s)
	: ioc_(s.ioc_)
	, resolver_(std::move(s.resolver_))
	, ssl_ctx_(s.ssl_ctx_)
	, default_server_(std::move(s.default_server_)) {
	// std::cerr << "shock move construct\n";
    }

    void acl_add_user(const std::string &node_url, const AuthToken &token, const std::string &username, boost::asio::yield_context yield) {
	URL url(node_url);
	url.path_append("/acl/all");
	url.query("users=" + username);
	std::cerr << url.construct() << "\n";

	if (url.protocol() == "http")
	    request("PUT", url, token, "", yield, nullptr);
	else  if (url.protocol() == "https")
	    request_ssl("PUT", url, token, "", yield, nullptr);

    }

    boost::json::object get_node(const AuthToken &token, const std::string &url_str, boost::asio::yield_context yield) {
	URL url(url_str);

	boost::json::object node_obj;
	
	auto cb = [&node_obj](boost::json::object obj) {
	    std::cerr << "get node got " << obj << "\n";
	    
	    try {
		node_obj = obj["data"].as_object();
	    } catch (std::invalid_argument e) {
		std::cerr << "error extracting node id: " << e.what() << "\n";
	    }
	};

	// this is a hack; we really need to have a form interface here.
	if (url.protocol() == "http")
	    request("GET", url, token, "", yield, cb);
	else  if (url.protocol() == "https")
	    request_ssl("GET", url, token, "", yield, cb);
	return node_obj;
    }

    std::string create_node(const AuthToken &token, const std::string &ws_node, boost::asio::yield_context yield) {
	URL url(default_server_ + "/node");

	std::string node;
	auto cb = [&node](boost::json::object obj) {
	    std::cerr << "create node got " << obj << "\n";

	    try {
		auto data = obj["data"].as_object();
		node = data["id"].as_string().c_str();
	    } catch (std::invalid_argument e) {
		std::cerr << "error extracting node id: " << e.what() << "\n";
		node = "";
	    }
	};

	// this is a hack; we really need to have a form interface here.
	std::string body = "{\"ws_id\": [\"" + ws_node + "\"]}";
	if (url.protocol() == "http")
	    request("POST", url, token, body, yield, cb);
	else  if (url.protocol() == "https")
	    request_ssl("POST", url, token, body, yield, cb);
	return node;
    }

    template <typename Buf>
    void start_download(const std::string &node_url, const std::string &token,
			boost::beast::tcp_stream &stream, Buf &buffer, boost::system::error_code &ec, boost::asio::yield_context yield) {

	namespace beast = boost::beast;         // from <boost/beast.hpp>
	namespace http = beast::http;           // from <boost/beast/http.hpp>
	namespace net = boost::asio;            // from <boost/asio.hpp>
	namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
	using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>



	// We will parse out the node ID and use the internal Shock address to pull the data
	static const boost::regex shock_node_regex(".*/node/([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
	boost::smatch match;
	if (!boost::regex_match(node_url, match, shock_node_regex))
	{
	    ec = WorkspaceErrc::InvalidServiceRequest;
	    return;
	}

	std::cerr << "node is " << match[1] << "\n";

	// TODO make this a config item
	URL dl_url{"http://walnut.mcs.anl.gov:7078/node/" + match[1] + "?download"};
	std::cerr << "dl " << dl_url << "\n";
    
	http::request<http::empty_body> req;

	req.method_string("GET");
	req.target(dl_url.path_query());
	req.set(http::field::host, dl_url.domain());
	req.set(http::field::user_agent, "p4x-shock");
	req.set(http::field::authorization, "OAuth " + token);

	boost::asio::ip::tcp::resolver resolver { ioc_ };

	auto fail = [](beast::error_code ec, char const* what) {
	    std::cerr << what << ": " << ec.message() << "\n";
	};


	auto res_iter = resolver.async_resolve(dl_url.domain(), dl_url.port(), yield[ec]);
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

	http::response_parser<http::string_body> header_parser;
	header_parser.body_limit(1000000000);
    
	http::async_read_header(stream, buffer, header_parser, yield[ec]);
	if (ec)
	{
	    fail(ec, "read");
	}
	auto &resp = header_parser.get();
	std::cerr << "shock read" << resp.base() << "\n";
    
    }

    void request(const std::string &method, const URL &url, const AuthToken &token,
		 const std::string &body, boost::asio::yield_context yield,
		 std::function<void(boost::json::object)> cb);
    void request_ssl(const std::string &method, const URL &url, const AuthToken &token,
		     const std::string &body, boost::asio::yield_context yield,
		     std::function<void(boost::json::object)> cb);
	  
};


#endif
