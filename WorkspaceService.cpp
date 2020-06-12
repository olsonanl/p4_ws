#include "WorkspaceService.h"
#include "WorkspaceDB.h"

#include <boost/regex.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

namespace json = boost::json;

// Report a failure
static void
fail(boost::system::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

bool value_as_bool(const json::value &v)
{
    switch (v.kind())
    {
    case json::kind::int64:
	return v.as_int64();

    case json::kind::uint64:
	return v.as_uint64();

    case json::kind::bool_:
	return v.as_bool();

    default:
	throw std::invalid_argument("not convertible to bool");
    }
}

bool object_at_as_bool(const json::object &obj, const json::string &key,
		       bool default_value = false)
{
    auto iter = obj.find(key);
    if (iter != obj.end())
    {
	return value_as_bool(iter->value());
    }
    else
    {
	return default_value;
    }
}

void WorkspaceService::method_ls(const JsonRpcRequest &req, JsonRpcResponse &resp,
				 DispatchContext &dc, boost::system::error_code &ec)
{
    ec = {};
    
    auto input = req.params().at(0).as_object();
    auto paths = input.at("paths").as_array();
    bool excludeDirectories = input.at("excludeDirectories").as_bool();
    bool excludeObjects = input.at("excludeObjects").as_bool();
    bool recursive = input.at("recursive").as_bool();
    bool fullHierachicalOutput = input.at("fullHierachicalOutput").as_bool();
    
    
    for (auto path: paths)
    {
    }
}

void WorkspaceService::method_get(const JsonRpcRequest &req, JsonRpcResponse &resp,
				  DispatchContext &dc, boost::system::error_code &ec)
{
    ec = {};

    bool metadata_only = false;
    bool adminmode = false;
    json::array objects;

    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();

	metadata_only = object_at_as_bool(input, "metadata_only");
	adminmode =  object_at_as_bool(input, "adminmode");
	
    } catch (std::invalid_argument e) {
	std::cerr << "error parsing: " << e.what() << "\n";
	ec = WorkspaceErrc::InvalidServiceRequest;
	return;
    }

    std::cerr << "metadata_only=" << metadata_only << " adminmode=" << adminmode << "\n";

    json::array output;
    resp.result().emplace_back(output);
    for (auto obj: objects)
    {
	std::cerr << "check " << obj << "\n";
	if (obj.kind() != json::kind::string)
	{
	    ec = WorkspaceErrc::InvalidServiceRequest;
	    return;
	}

	WSPath path;
	ObjectMeta meta;

	global_state_->db()->run_in_thread(dc,
					   [&path, str = obj.as_string(), &meta]
					   (std::unique_ptr<WorkspaceDBQuery> qobj) 
		{
		    std::cerr << "in mongo thread\n";
		    path = qobj->parse_path(str);

		    meta = qobj->lookup_object_meta(path);
		});
	std::cerr << "parsed path " << path << "\n";
	json::array obj_output( { meta.serialize(), "" });
	output.emplace_back(obj_output);
    }
    std::cerr << output << "\n";
}

