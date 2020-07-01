#include "HTTPServer.h"
#include <thread>
#include <vector>

#include <boost/asio/error.hpp>

#include "parse_url.h"
#include "Shock.h"

using namespace ws_http_server;

void Server::run(net::ip::address address, unsigned short port, int threads) {
	    
    boost::asio::spawn(ioc_,
		       [listener = std::make_shared<Listener>(ioc_,
							      tcp::endpoint{address, port},
							      *this)](net::yield_context yield) {
			   listener->run(yield);
		       });

    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for (auto i = threads - 1; i > 0; --i)
    {
	v.emplace_back([this] { ioc_.run(); });
    }
	
    ioc_.run();

    // Block until all the threads exit
    for (auto& t : v)
	t.join();
}

void Session::handle_get(decltype(Session::header_parser_)::value_type &req, net::yield_context yield) {
    auto path = req.target();
    if (path == "/quit")
    {
	server_.quit_server();
	stream_.close();
    }
    else
    {
	// 50e67cb9-b680-42a6-b031-c5251cbca298
	static const boost::regex dl_regex("/dl/([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})/([^/]+)");

	boost::match_results<boost::string_view::const_iterator> match;

	if (boost::regex_match(path.begin(), path.end(), match, dl_regex))
	{
	    std::cerr << "Have DL match " << match[1] << " with name " << match[2] << "\n";

	    std::string url_filename;
	    url_decode(match[2], url_filename);

	    wslog::logger disp_logger(wslog::channel = name_logger("dispatch", stream_.socket()));
	    DispatchContext dc(yield, stream_.get_executor(), token_, disp_logger);
	    bool ok;
	    std::string name, shock_node, token, file_path;
	    size_t size{0L};
	    server_.workspace_service().db().run_in_thread(dc, [this, &match, &ok,
								&name, &shock_node, &token, &file_path, &size]
							   (std::unique_ptr<WorkspaceDBQuery> qobj)
		{
		    ok = qobj->lookup_download(match[1], name, size, shock_node, token, file_path);
		});
	    if (!ok)
	    {
		std::cerr << "lookup failed\n";
		error_response(404, "File not found", yield);
		return;
	    }
	    else if (url_filename != name)
	    {
		std::cerr << "name mismatch\n";
		error_response(404, "File not found", yield);
		return;
	    }
	    else if (file_path.empty())
	    {
		std::cerr << "got shock " << name << " " << size << " " << shock_node << " " << token << "\n";

		http::response<http::empty_body> resp{http::status::ok, 11};
		resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		resp.set(http::field::content_type, "application/octet-stream");
		resp.set(http::field::content_length, size);
		resp.set(http::field::content_disposition, "attachment; filename=" + name);
		resp.keep_alive(false);

		http::response_serializer<http::empty_body> sr{resp};

		boost::system::error_code ec;
		
		http::async_write_header(stream_, sr, yield[ec]);
		if (ec)
		{
		    fail(ec, "shock header write");
		}

		// Set up to stream shock data.
		const int buf_size = 100000;
		beast::flat_buffer buf{ buf_size };	
		beast::tcp_stream shock_stream{ net::make_strand(server_.ioc()) };
		server_.shock().start_download(shock_node, token, shock_stream, buf, ec, yield);

		// We may have leftover bytes from reading the header.
		while (buf.size())
		{
		    int n_written = stream_.async_write_some(buf.data(), yield[ec]);
		    if (ec)
		    {
			fail(ec, "first async_write_some");
			return;
		    }
		    buf.consume(n_written);
		}

		while (1)
		{
		    auto n_bytes = shock_stream.async_read_some(buf.prepare(buf_size), yield[ec]);
		    buf.commit(n_bytes);
		    if (ec == net::error::eof || n_bytes == 0)
			break;
		    if (ec)
		    {
			fail(ec, "async_read_some");
			stream_.close();
			return;
		    }

		    while (buf.size() > 0)
		    {
			int n_written = stream_.async_write_some(buf.data(), yield[ec]);
			if (ec)
			{
			    fail(ec, "async_write_some");
			    stream_.close();
			    return;
			}
			if (n_written == 0)
			{
			    std::cerr << "empty write\n";
			    stream_.close();
			    return;
			}
			buf.consume(n_written);
		    }
		}
		stream_.close();
		return;
	    }
	    else
	    {
		std::cerr << "got file " << name << " " << size << " " << file_path << "\n";
		http::file_body::value_type file;
		boost::system::error_code ec;
		file.open(file_path.c_str(), boost::beast::file_mode::read, ec);
		if (ec)
		{
		    fail(ec, "file open failed");
		    error_response(404, "File not found", yield);
		    return;
		}
		http::response<http::file_body> resp;
		resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		resp.set(http::field::content_type, "application/octet-stream");
		resp.set(http::field::content_length, size);
		resp.set(http::field::content_disposition, "attachment; filename=" + name);
		resp.keep_alive(false);
		resp.body() = std::move(file);
		resp.prepare_payload();
		http::async_write(stream_, resp, yield[ec]);
		if (ec)
		{
		    fail(ec, "file write failed");
		}
	    }
	}
    }
}

