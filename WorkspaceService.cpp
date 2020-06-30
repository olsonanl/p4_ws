#include "WorkspaceService.h"
#include "WorkspaceDB.h"
#include "WorkspaceConfig.h"

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
    json::array paths;
    bool excludeDirectories, excludeObjects, recursive, fullHierachicalOutput;

    try {
	auto input = req.params().at(0).as_object();
	paths = input.at("paths").as_array();
	excludeDirectories = object_at_as_bool(input, "excludeDirectories");
	excludeObjects = object_at_as_bool(input, "excludeObjects");
	recursive = object_at_as_bool(input, "recursive");
	fullHierachicalOutput = object_at_as_bool(input, "fullHierachicalOutput");
	
	if (object_at_as_bool(input, "adminmode"))
	    dc.admin_mode = config().user_is_admin(dc.token.user());
    } catch (std::invalid_argument e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    } catch (std::exception e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    }
    
    // Validate format of paths before doing any work
    for (auto path: paths)
    {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "check " <<path << "\n";
	if (path.kind() != json::kind::string)
	{
	    resp.set_error(-32602, "Invalid request parameters");
	    http_code = 500;
	    return;
	}
    }

    // We will run essentially this entire command in the
    // database thread as the work is all database-based
    
    json::object output;
    global_state_->db().run_in_thread(dc,
				       [this, &paths, &output, &dc,
					excludeDirectories, excludeObjects, recursive, fullHierachicalOutput]
				       (std::unique_ptr<WorkspaceDBQuery> qobj) 
					   {
		    // wslog::logger l(wslog::channel = "mongo_thread");
					       process_ls(std::move(qobj), dc, paths, output,
							  excludeDirectories, excludeObjects, recursive, fullHierachicalOutput);
					   });
    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}


// This executes in a WorkspaceDB thread

void WorkspaceService::process_ls(std::unique_ptr<WorkspaceDBQuery> qobj,
				  DispatchContext &dc, json::array &paths, json::object &output,
				  bool excludeDirectories, bool excludeObjects, bool recursive, bool fullHierachicalOutput)
{
    for (auto path_jobj: paths)
    {
	auto path_str = path_jobj.as_string();
	WSPath path = qobj->parse_path(path_str);
	std::cerr << path << "\n";

	std::vector<ObjectMeta> list;

	if (path.empty)
	{
	    // Path did not parse. One reason is that it was a request for "/".
	    if (path_str == "/")
	    {
		list = qobj->list_workspaces("");
	    }
	    else
	    {
		BOOST_LOG_SEV(dc.lg_, wslog::notification) << "Path did not parse: " << path_str << "\n";
		continue;
	    }
	}
	else if (path.workspace.name.empty())
	{
	    list = qobj->list_workspaces(path.workspace.owner);
	}
	else if (qobj->user_has_permission(path.workspace, WSPermission::read))
	{
	    list = qobj->list_objects(path, excludeDirectories, excludeObjects, recursive);
	}
	json::array jlist;
	for (auto elt: list)
	    jlist.emplace_back(elt.serialize());
	    
	output.emplace(path_str, jlist);
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
	    dc.admin_mode = config().user_is_admin(dc.token.user());

    } catch (std::invalid_argument e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    } catch (std::exception e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    }

    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "metadata_only=" << metadata_only << " adminmode=" << dc.admin_mode << "\n";

    // Validate format of paths before doing any work
    for (auto obj: objects)
    {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "check " << obj << "\n";
	if (obj.kind() != json::kind::string)
	{
	    resp.set_error(-32602, "Invalid request parameters");
	    http_code = 500;
	    return;
	}
    }

    json::array output;

    for (auto obj: objects)
    {
	WSPath path;
	ObjectMeta meta;

	std::string file_data;
	global_state_->db().run_in_thread(dc,
					   [&path, path_str = obj.as_string(),
					    metadata_only, &meta]
					   (std::unique_ptr<WorkspaceDBQuery> qobj) 
		{
		    // wslog::logger l(wslog::channel = "mongo_thread");
		    
		    path = qobj->parse_path(path_str);
		    if (path.workspace.name.empty() ||
			qobj->user_has_permission(path.workspace, WSPermission::read))
		    {
			meta = qobj->lookup_object_meta(path);
		    }
		});
	// We do Shock manipulations back in the main thread of control
	// so we can take advantage of the asynch support.

	std::cerr << "OM: " << meta << "\n";

	if (meta.valid)
	{
	    if (!metadata_only)
	    {
		if (meta.shockurl.empty())
		{
		    // We may want to move file I/O into a file I/O thread pool
		    std::string fs_path = global_state_->config().filesystem_path_for_object(path);
		    std::cerr << "retrieve data from " << fs_path << "\n";
		    std::ifstream f(fs_path);
		    if (f)
		    {
			std::ostringstream ss;
			ss << f.rdbuf();
			file_data = std::move(ss.str());
		    }
		    else
		    {
			BOOST_LOG_SEV(dc.lg_, wslog::error) << "cannot read WS file path " << fs_path << " from object " << path << "\n";
		    }
		}
		else
		{
		    std::cerr  << "invoke shock " << dc.token << "\n";

		    // This token needs to be the shock data owner token
		    AuthToken &token = global_state_->ws_auth(dc.yield);
		    std::cerr << "got ws auth " << token << "\n";
		    global_state_->shock().acl_add_user(meta.shockurl, token, dc.yield);
		    std::cerr  << "invoke shock..done\n";
		}
	    }
	    
	}
	json::array obj_output( { meta.serialize(), file_data });
	output.emplace_back(obj_output);
    }
    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

void WorkspaceService::method_list_permissions(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code)
{
    json::array objects;

    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();

	if (object_at_as_bool(input, "adminmode"))
	    dc.admin_mode = config().user_is_admin(dc.token.user());

    } catch (std::invalid_argument e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    } catch (std::exception e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    }

    // Validate format of paths before doing any work
    for (auto obj: objects)
    {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "check " << obj << "\n";
	if (obj.kind() != json::kind::string)
	{
	    resp.set_error(-32602, "Invalid request parameters");
	    http_code = 500;
	    return;
	}
    }

    json::object output;

    global_state_->db().run_in_thread(dc,
				       [objects, &output]
				       (std::unique_ptr<WorkspaceDBQuery> qobj) 
       	{
	    for (auto obj: objects)
	    {
		
		auto path_str = obj.as_string();
		WSPath path = qobj->parse_path(path_str);
		json::object obj_output{};
		json::array perms{};
		if (path.workspace.name.empty() ||
		    qobj->user_has_permission(path.workspace, WSPermission::read))
		{
		    path.workspace.serialize_permissions(perms);
		}
		output.emplace(path_str, perms);
	    }
	});

    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

void WorkspaceService::method_get_download_url(const JsonRpcRequest &req, JsonRpcResponse &resp,
				  DispatchContext &dc, int &http_code)
{
    json::array objects;

    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();
    } catch (std::invalid_argument e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    } catch (std::exception e) {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	resp.set_error(-32602, "Invalid request parameters");
	http_code = 500;
	return;
    }

    // Validate format of paths before doing any work
    for (auto obj: objects)
    {
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "check " << obj << "\n";
	if (obj.kind() != json::kind::string)
	{
	    resp.set_error(-32602, "Invalid request parameters");
	    http_code = 500;
	    return;
	}
    }

    json::array output;

    /*
     * For each object, validate the path and ensure it points to a file
     * and not a folder. Then create the unique download ID and register with the
     * database.
     *
     * We need to collect the Shock URLs to ensure access is available for them.
     * We don't just collect the URL out of the meta objects because a decision
     * was made inside the insert_download_for_object code to see if the 
     * current user had a valid token; only then do we add the Shock url to the list.
     */

    AuthToken &ws_auth = global_state_->ws_auth(dc.yield);
    std::vector<std::string> shock_urls;
    
    auto work = [&dc, &objects, &output, &ws_auth, &shock_urls, this] (std::unique_ptr<WorkspaceDBQuery> qobj)
	{
	    for (auto obj: objects)
	    {
		auto path_str = obj.as_string();
		ObjectMeta meta;
		std::string key = qobj->insert_download_for_object(path_str, ws_auth, meta, shock_urls);
		if (key.empty())
		{
		    output.emplace_back(key);
		}
		else
		{
		    std::string encoded_name;
		    url_encode(meta.name, encoded_name);
		    output.emplace_back(config().download_url_base() + "/" + key + "/" + encoded_name);
		}
		    
	    }
	};

    global_state_->db().run_in_thread(dc, work);

    for (auto url: shock_urls)
    {
	global_state_->shock().acl_add_user(url, dc.token, dc.yield);
    }

    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

