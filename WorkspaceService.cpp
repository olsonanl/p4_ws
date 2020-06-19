#include "WorkspaceService.h"
#include "WorkspaceDB.h"

#include <boost/regex.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

namespace json = boost::json;

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
				 DispatchContext &dc, int &http_code)
{
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
				  DispatchContext &dc, int &http_code)
{
    bool metadata_only = false;
    json::array objects;

    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();

	metadata_only = object_at_as_bool(input, "metadata_only");
	if (object_at_as_bool(input, "adminmode"))
	{
	    dc.admin_mode = config().user_is_admin(dc.token.user());
	}

    } catch (std::invalid_argument e) {
	BOOST_LOG_SEV(lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    }

    BOOST_LOG_SEV(lg_, wslog::debug) << "metadata_only=" << metadata_only << " adminmode=" << dc.admin_mode << "\n";

    json::array output;

    // Validate format of paths before doing any work
    for (auto obj: objects)
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "check " << obj << "\n";
	if (obj.kind() != json::kind::string)
	{
	    resp.set_error(-32602, "Invalid request parameters");
	    http_code = 500;
	    return;
	}
    }

    for (auto obj: objects)
    {
	WSPath path;
	ObjectMeta meta;

	global_state_->db()->run_in_thread(dc,
					   [&path, path_str = obj.as_string(),
					    metadata_only, &meta]
					   (std::unique_ptr<WorkspaceDBQuery> qobj) 
		{
		    // wslog::logger l(wslog::channel = "mongo_thread");
		    
		    path = qobj->parse_path(path_str);
		    if (qobj->user_has_permission(path.workspace, WSPermission::read))
		    {
			meta = qobj->lookup_object_meta(path);
			if (!metadata_only)
			{
			}
		    }
		});
	BOOST_LOG_SEV(lg_, wslog::debug) << "parsed path " << path << "\n";
	json::array obj_output( { meta.serialize(), "" });
	output.emplace_back(obj_output);
    }
    resp.result().emplace_back(output);
    BOOST_LOG_SEV(lg_, wslog::debug) << output << "\n";
}

