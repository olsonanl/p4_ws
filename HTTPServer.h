#ifndef _HTTPServer_h
#define _HTTPServer_h

#include <boost/beast/core.hpp>
#include <boost/beast/core/flat_static_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/config.hpp>

#include <boost/json.hpp>
#include <boost/regex.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <functional>

#include "Logging.h"
#include "ServiceDispatcher.h"
#include "DispatchContext.h"
#include "AuthToken.h"
#include "WorkspaceService.h"
#include "WorkspaceDB.h"

namespace ws_http_server {

    namespace beast = boost::beast;         // from <boost/beast.hpp>
    namespace http = beast::http;           // from <boost/beast/http.hpp>
    namespace net = boost::asio;            // from <boost/asio.hpp>
    using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
    namespace json = boost::json;
    namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>

    class Server
    {
	net::io_context &ioc_;
	std::string api_root_;
	ServiceDispatcher &dispatcher_;
	WorkspaceService &workspace_service_;
	bool quit_;
    public:
	explicit Server(net::io_context &ioc, std::string api_root, ServiceDispatcher &dispatcher, WorkspaceService &svc)
	    : ioc_(ioc)
	    , api_root_(api_root)
	    , dispatcher_(dispatcher)
	    , workspace_service_(svc)
	    , quit_(false) {
	}
	
	void run(net::ip::address address, unsigned short port, int threads);
	bool quit() { return quit_; }
	void quit_server() { quit_ = true; }
	ServiceDispatcher &dispatcher() { return dispatcher_; }
	WorkspaceService &workspace_service() { return workspace_service_; }
	const std::string &api_root() { return api_root_; }
    };

    inline std::string name_logger(const std::string &base, const tcp::socket &sock)
    {
	auto peer = sock.remote_endpoint();
	std::ostringstream ostr;
	ostr << base << ":" << peer;
	return std::move(ostr.str());
    }
    
    /*
     * Session class. Read and process HTTP requests.
     */
    class Session
	: public std::enable_shared_from_this<Session>
	, public wslog::LoggerBase
    {
	Server &server_;
	beast::tcp_stream stream_;
	beast::flat_static_buffer<10000> buf_;
	http::request_parser<http::buffer_body> header_parser_;
	std::shared_ptr<void> res_;
	std::array<char, 10240> buffer_;
	
	AuthToken token_;
	
    public:
	explicit Session(tcp::socket &&socket,
			 Server &server)
	    : server_(server)
	    , stream_(std::move(socket))
	    , wslog::LoggerBase(name_logger("session", socket)) {
	    header_parser_.body_limit(1000000);
	}

	void run(net::yield_context yield) {
	    loop(yield);
	}

    private:

	/*
	 * GET requests.
	 * We for now have a /quit that quits the server
	 * TODO this should require admin authentication or connect only from localhost.
	 *
	 * The /dl resource handles the downloads as defined by the
	 * Workspace.get_download_url method. It expects URLs of the form
	 * /dl/{UUID}/filename
	 */

	void handle_get(decltype(header_parser_)::value_type &req, net::yield_context yield);
	
	void error_response(int code, const std::string &msg, net::yield_context yield) {
	    http::response<http::empty_body> http_resp;
	    http_resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
	    http_resp.set(http::field::content_type, "text/plain");
	    http_resp.result(code);
	    http_resp.prepare_payload();
	    boost::system::error_code ec;
	    http::async_write(stream_, http_resp, yield[ec]);
	    if (ec)
	    {
		fail(ec, "response write");
	    }
	}

	void handle_options(decltype(header_parser_)::value_type &req, net::yield_context yield) {

	    http::response<http::empty_body> http_resp;
	    http_resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
	    http_resp.set(http::field::content_type, "text/plain");

	    auto &org = req[http::field::origin];
	    if (!org.empty())
		http_resp.set(http::field::access_control_allow_origin, org);
	    auto &methods = req[http::field::access_control_request_method];
	    if (!methods.empty())
		http_resp.set(http::field::access_control_allow_methods, methods);
	    auto &hdrs = req[http::field::access_control_request_headers];
	    if (!hdrs.empty())
		http_resp.set(http::field::access_control_allow_headers, hdrs);
	    http_resp.set(http::field::access_control_max_age, "86400");
	
	    http_resp.result(204);
	
	    http_resp.prepare_payload();

	    boost::system::error_code ec;
	    http::async_write(stream_, http_resp, yield[ec]);
	    if (ec)
	    {
		fail(ec, "response write");
	    }
	
	}

	void handle_api(decltype(header_parser_)::value_type &req, net::yield_context yield) {
	    beast::error_code ec;
	
	    json::parser parser;

	    parser.start();

	    auto &message = header_parser_.get();
	    auto &body_remaining = message.body();

	    body_remaining.data = buffer_.data();
	    body_remaining.size = buffer_.size();

	    while (body_remaining.size && !header_parser_.is_done())
	    {
		http::async_read_some(stream_, buf_, header_parser_, yield[ec]);
		BOOST_LOG_SEV(lg_, wslog::debug) << "read returns " << ec << "\n";
		if (ec == http::error::need_buffer || ec == http::error::need_buffer)
		{
		}
		else if (ec)
		{
		    fail(ec, "async_read");
		    return;
		}
		size_t n = buffer_.size() - body_remaining.size;
	    
		// std::cerr << "Read: " << n << std::string(buffer_.data(), n);
		try {
		    parser.write(static_cast<const char *>(buffer_.data()), n);
		} catch (std::exception e) {
		    BOOST_LOG_SEV(lg_, wslog::error) << "jsonrpc parse error " << e.what() << "\n";
		    return;
		}
		
		if (ec && ec != http::error::need_buffer)
		{
		    fail(ec, "json parse");
		    return;
		}
		body_remaining.data = buffer_.data();
		body_remaining.size = buffer_.size();
	    }

	    parser.finish();
	    if (parser.is_complete())
	    {
		// std::cerr << "parse complete\n";
		auto v = parser.release();
		// std::cerr << v << "\n";
		JsonRpcRequest rpc_req;
		rpc_req.parse(v, ec);
		if (ec)
		{
		    fail(ec, "request parse");
		    return;
		}
		BOOST_LOG_SEV(lg_, wslog::debug) << "request: " << rpc_req << "\n";
		JsonRpcResponse rpc_resp(rpc_req);


		wslog::logger disp_logger(wslog::channel = name_logger("dispatch", stream_.socket()));
		DispatchContext dc(yield, stream_.get_executor(), token_, disp_logger);

		int http_code = 200;
		server_.dispatcher().dispatch(rpc_req, rpc_resp, dc, http_code);

		boost::json::value response_value = rpc_resp.full_response();

		BOOST_LOG_SEV(lg_, wslog::debug) << "Req " << rpc_req << " had code " << http_code << " with response " << response_value << "\n";

		/*
		 * Chunked responses will take some more thought.
	     
		 boost::json::serializer sr(response_value);
		 while (!sr.is_done())
		 {
		 char buf[10240];
		 auto const n = sr.read(buf, sizeof(buf));
		 }
		*/

		http::response<http::string_body> http_resp;
		boost::json::string response_string = boost::json::to_string(response_value);
		http_resp.body() = std::string(response_string.data(), response_string.size());
		http_resp.set(http::field::access_control_allow_origin, "*");
		http_resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		http_resp.set(http::field::content_type, "application/json");
		http_resp.set(http::field::connection, "close");
		http_resp.result(http_code);
		http_resp.prepare_payload();
		http::async_write(stream_, http_resp, yield[ec]);
		if (ec)
		{
		    fail(ec, "response write");
		}
		auto peer = stream_.socket().remote_endpoint(ec);
		BOOST_LOG_SEV(lg_, wslog::debug) << "Completed request from " << peer;
		stream_.close();
	    }
	    else
	    {
		BOOST_LOG_SEV(lg_, wslog::notification) << "at end of stream parse was not complete\n";
	    }
	
	}

	void loop(net::yield_context yield) {

	    beast::error_code ec;
	
	    http::async_read_header(stream_, buf_, header_parser_, yield[ec]);

	    if (ec)
	    {
		fail(ec, "async_read_header");
		return;
	    }
	    auto &req = header_parser_.get();
	    // std::cerr << "incoming: " << req.base() << "\n";

	    // Handle Expect: 100-continue if present
	    if (req[http::field::expect] == "100-continue")
	    {
		// std::cerr << " process continue\n";

		http::response<http::empty_body> res;
		res.version(11);
		res.result(http::status::continue_);
		res.set(http::field::server, "tesT");
		http::async_write(stream_, res, yield[ec]);
		if (ec)
		{
		    fail(ec, "async_write 100-continue");
		    return;
		}
	    }

	    /*
	     * Extract token from headers if present.
	     */
	    auto auth_hdr = req["Authorization"];
	    if (auth_hdr != "")
	    {
		token_.parse(auth_hdr);
		// We just parse into the token here.
		// The decision about if we even need to validate
		// it is left to the called code
	    }

	    /*
	     * Process requests. We vector GET to handle_get,
	     * POST to the API URL to handle_api,
	     * and mark anything else as an error.
	     */
       
	    if (req.method() == http::verb::get)
	    {
		handle_get(req, yield);
	    }
	    if (req.method() == http::verb::options)
	    {
		if (req.target() == server_.api_root())
		{
		    handle_options(req, yield);
		}
	    }
	    else if (req.method() == http::verb::post)
	    {
		if (req.target() == server_.api_root())
		{
		    //std::cerr << "handle api req\n";

		    handle_api(req, yield);
		}
	    }
	}
    };

    /*
     * Listener class. Await connections and hand them off to the session.
     */
    class Listener
	: public std::enable_shared_from_this<Listener>
	, public wslog::LoggerBase
    {
	net::io_context & ioc_;
	tcp::acceptor acceptor_;
	tcp::socket socket_;
	Server &server_;
    public:
	Listener(net::io_context &ioc,
		 tcp::endpoint endpoint,
		 Server &server)
	    : ioc_(ioc)
	    , acceptor_(net::make_strand(ioc))
	    , socket_(net::make_strand(ioc))
	    , server_(server)
	    , wslog::LoggerBase("listener") {
	    
	    beast::error_code ec;
	    
	    // Open the acceptor.
	    acceptor_.open(endpoint.protocol(), ec);
	    if (ec)
	    {
		fail(ec, "open");
		return;
	    }

	    // Set option to allow address reuse
	    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
	    if (ec)
	    {
		fail(ec, "set_option");
		return;
	    }

	    // Bind to address and write listening port
	    acceptor_.bind(endpoint, ec);
	    if (ec)
	    {
		fail(ec, "bind");
		return;
	    }
	    auto local = acceptor_.local_endpoint(ec);
	    if (ec)
	    {
		fail(ec, "local_endpoint");
		return;
	    }
	    BOOST_LOG_SEV(lg_, wslog::debug) << "listening on " << local << "\n";

	    // Start listening
	    acceptor_.listen(net::socket_base::max_listen_connections, ec);
	    if (ec)
	    {
		fail(ec, "listen");
		return;
	    }
	}

	void run(net::yield_context yield) {
	    loop(yield);
	}

    private:
    
	void loop(net::yield_context yield) {
	    beast::error_code ec = {};
	    while (!server_.quit())
	    {
		acceptor_.async_accept(socket_, yield[ec]);
		if (ec)
		{
		    fail(ec, "accept");
		    break;
		}
		else
		{
		    auto peer = socket_.remote_endpoint(ec);
		    if (ec)
		    {
			fail(ec, "remote_endpoint");
		    }
		
		    BOOST_LOG_SEV(lg_, wslog::debug) << "connection from " << peer << "\n";
		
		
		    // Create the session and run it

		    boost::asio::spawn(acceptor_.get_executor(),
				       [session = std::make_shared<Session>(std::move(socket_), server_)]
				       (net::yield_context yield) {
					   session->run(yield);
				       });
		}
	    
		// Make sure each session gets its own strand
		socket_ = tcp::socket(net::make_strand(ioc_));
	    }
	}

    };
}

#endif
