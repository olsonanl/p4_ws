#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
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
    : public boost::asio::coroutine
    , public std::enable_shared_from_this<Session>
{
    beast::tcp_stream stream_;
    beast::flat_buffer buf_;
    std::shared_ptr<WorkspaceState> state_;
    http::request_parser<http::empty_body> header_parser_;
    std::shared_ptr<void> res_;
    
public:
    explicit Session(tcp::socket &&socket,
		     std::shared_ptr<WorkspaceState> state)
	: stream_(std::move(socket))
	, buf_{10000}
	, state_{state} {
    }

    void run() {
	net::dispatch(stream_.get_executor(),
		      beast::bind_front_handler(&Session::loop,
						shared_from_this(),
						beast::error_code{},
						0));
    }

private:
    
    #include <boost/asio/yield.hpp>
    
    void loop(beast::error_code ec, std::size_t bytes_transferred) {
	boost::ignore_unused(bytes_transferred);
	
	reenter(*this)
	{
	    for(;;)
	    {
		yield http::async_read_header(stream_, buf_, header_parser_,
					      beast::bind_front_handler(&Session::loop,
									shared_from_this()));
		if (ec)
		{
		    fail(ec, "async_read");
		    return;
		}
		auto &req_ = header_parser_.get();
		std::cerr << "incoming: " << bytes_transferred << " " << req_ << "\n";

		if (req_.method() == http::verb::get)
		{
		    std::cerr << "GET " << req_.target() << "\n";
		}
		else if (req_.method() == http::verb::post)
		{
		    if (req_.target() == state_->api_root())
		    {
			std::cerr << "handle api req\n";
			yield http::async_read(stream_, buf_,
					       beast::bind_front_handler(&Session::loop,
									 shared_from_this()));
			
		    }
		}
		return;
	    }
	}
    }

    #include <boost/asio/unyield.hpp>
};

/*
 * Listener class. Await connections and hand them off to the session.
 */
class Listener
    : public boost::asio::coroutine
    , public std::enable_shared_from_this<Listener>
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

    void run() {
	loop();
    }

private:
    
    #include <boost/asio/yield.hpp>
    
    void loop(beast::error_code ec = {}) {
	reenter(*this)
	{
	    for(;;)
	    {
		yield acceptor_.async_accept(socket_,
					     beast::bind_front_handler(
						 &Listener::loop,
						 shared_from_this()));
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
		    std::make_shared<Session>(std::move(socket_),
					      state_)->run();
		}
		
		// Make sure each session gets its own strand
		socket_ = tcp::socket(net::make_strand(ioc_));
	    }
	}
    }

    #include <boost/asio/unyield.hpp>
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

    std::make_shared<Listener>(ioc,
			       tcp::endpoint{address, port},
			       global_state)->run();

    ioc.run();
    
    return EXIT_SUCCESS;
}
