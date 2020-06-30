#include "HTTPServer.h"
#include <thread>
#include <vector>

#include "parse_url.h"

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
		error_response(404, "File not found", yield);
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

