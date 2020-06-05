#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/config.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

#include "WorkspaceState.h"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

// Report a failure
void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

/*
 * Session class. Read and process HTTP requests.
 */
class Session
    : public std::enable_shared_from_this<Session>
{
    beast::tcp_stream stream_;
    beast::flat_buffer buf_;
    std::shared_ptr<WorkspaceState> state_;
    http::request_parser<http::string_body> header_parser_;
    std::shared_ptr<void> res_;
    
public:
    explicit Session(tcp::socket &&socket,
		     std::shared_ptr<WorkspaceState> state)
	: stream_(std::move(socket))
	, buf_{10000}
	, state_{state} {
	    header_parser_.body_limit(1000000);
    }

    void run(net::yield_context yield) {
	loop(yield);
    }

private:
    
    void handle_get(decltype(header_parser_)::value_type &req, net::yield_context yield) {
    }

    void handle_api(decltype(header_parser_)::value_type &req, net::yield_context yield) {
	beast::error_code ec;
	if (buf_.size())
	{
	    std::cerr << buf_.size() << "\n";
	    buf_.consume(buf_.size());
	}
	
	while (1)
	{
	    stream_.async_read_some(buf_.data(), yield[ec]);
	    if (ec)
	    {
		fail(ec, "async_read");
		return;
	    }
	    else if (buf_.size() == 0)
	    {
		std::cerr << "done\n";
		break;
	    }
	    std::cerr << buf_.size() << "\n";
	    buf_.consume(buf_.size());
	}
	
	std::cerr << "read buf\n";
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
	std::cerr << "incoming: " << req.base() << "\n";

	// Handle Expect: 100-continue if present
	if (req[http::field::expect] == "100-continue")
	{
	    std::cerr << " process continue\n";

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
	 * Process requests. We vector GET to handle_get,
	 * POST to the API URL to handle_api,
	 * and mark anything else as an error.
	 */
       
	if (req.method() == http::verb::get)
	{
	    handle_get(req, yield);
	}
	else if (req.method() == http::verb::post)
	{
	    if (req.target() == state_->api_root())
	    {
		std::cerr << "handle api req\n";

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
{
    net::io_context & ioc_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
    std::shared_ptr<WorkspaceState> state_;
    
public:
    Listener(net::io_context &ioc,
	     tcp::endpoint endpoint,
	     std::shared_ptr<WorkspaceState> state)
	: ioc_(ioc)
	, acceptor_(net::make_strand(ioc))
	, socket_(net::make_strand(ioc))
	, state_(state) {

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
	std::cerr << "listening on " << local << "\n";

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
	for(;;)
	{
	    acceptor_.async_accept(socket_, yield[ec]);
	    if (ec)
	    {
		fail(ec, "accept");
	    }
	    else
	    {
		auto peer = socket_.remote_endpoint(ec);
		if (ec)
		{
		    fail(ec, "remote_endpoint");
		}
		
		std::cerr << "connection from " << peer << "\n";
		
		
		// Create the session and run it

		boost::asio::spawn(acceptor_.get_executor(),
				   [session = std::make_shared<Session>(std::move(socket_), state_)]
				   (net::yield_context yield) {
				       session->run(yield);
				   });
	    }
	    
	    // Make sure each session gets its own strand
	    socket_ = tcp::socket(net::make_strand(ioc_));
	}
    }

};


int main(int argc, char *argv[])
{
    if (argc != 3)
    {
	std::cerr << "Usage: " << argv[0] << " port threads\n";
	return EXIT_FAILURE;
    }

    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(std::atoi(argv[1]));
    auto const threads = std::max<int>(1, std::atoi(argv[2]));

    std::cerr << "Listening on " << port << " with " << threads << " threads\n";

    // IO context is required for I/O in C++ Networking

    net::io_context ioc{threads};

    // Create our global state container.

    auto global_state = std::make_shared<WorkspaceState>();

    // Create our listener and start it running.

    boost::asio::spawn(ioc,
		       [listener = std::make_shared<Listener>(ioc,
							      tcp::endpoint{address, port},
							      global_state)](net::yield_context yield) {
			   listener->run(yield);
		       });

    ioc.run();
    
    return EXIT_SUCCESS;
}
