#include "WorkspaceService.h"
#include "WorkspaceDB.h"
#include "WorkspaceConfig.h"
#include "WorkspaceState.h"

#include <boost/lexical_cast.hpp>

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

std::string object_at_as_string(const json::object &obj, const json::string &key,
				const std::string &default_value = "")
{
    auto iter = obj.find(key);
    if (iter != obj.end())
    {
	auto &val = iter->value();
	switch (val.kind())
	{
	case json::kind::int64:
	    return std::to_string(val.as_int64());

	case json::kind::uint64:
	    return std::to_string(val.as_int64());

	case json::kind::string:
	    return boost::lexical_cast<std::string>(val.as_string());

	default:
	    return default_value;
	}
    }
    else
    {
	return default_value;
    }
}

void WorkspaceService::dispatch(const JsonRpcRequest &req, JsonRpcResponse &resp,
				DispatchContext &dc, int &http_code)
{
    BOOST_LOG_SEV(lg_, wslog::debug) << "ws dispatching " << req << "\n";
    auto x = method_map_.find(req.method());
    if (x == method_map_.end())
    {
	http_code = 500;
	resp.set_error(-32601, "Method not found");
	return;
    }
    Method &method = x->second;
    
    /*
     * Manage auth.
     * If auth required or optional, attempt to verify token.
     * If auth not required, clear the token.
     */
    if (method.auth == Authentication::none)
    {
	dc.token.clear();
    }
    else
    {
	// Authentication is optional or required. Validate token.
	// If it does not validate, clear it.
	bool valid;
	try {
	    valid = shared_state_.validate_certificate(dc.token);
	} catch (std::exception e) {
	    BOOST_LOG_SEV(lg_, wslog::error) << "exception validating token: " << e.what() << "\n";
	    valid = false;
	}
	if (!valid)
	    dc.token.clear();
	
	if (method.auth == Authentication::required && !valid)
	{
	    // Fail
	    resp.set_error(503, "Authentication failed");
	    http_code = 403;
	    return;
	}
    }
    
    BOOST_LOG_SEV(lg_, wslog::notification) << "Dispatch: " << req;
    (this->*(method.method))(req, resp, dc, http_code);
}

void WorkspaceService::method_create(const JsonRpcRequest &req, JsonRpcResponse &resp,
				     DispatchContext &dc, int &http_code)
{
    json::array objects;
    std::string permission;
    bool createUploadNodes, downloadFromLinks;
    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();
	createUploadNodes = object_at_as_bool(input, "createUploadNodes");
	downloadFromLinks = object_at_as_bool(input, "downloadFromLinks");
	permission = object_at_as_string(input, "permission", "n");
	
	if (object_at_as_bool(input, "adminmode"))
	    dc.admin_mode = shared_state_.config().user_is_admin(dc.token.user());
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

    std::vector<ObjectToCreate> to_create;
    // Validate format of paths before doing any work
    for (auto obj: objects)
    {
	try {
	    to_create.emplace_back(obj);
	} catch (std::exception e) {
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	    resp.set_error(-32602, "Invalid request parameters");
	    http_code = 500;
	    return;
	}
    }

    /*
     * Objects have been parsed int ObjectToCreate list.
     * Pass to process_create in the database thread.
     */
    
    json::array output;
    db_.run_in_sync_thread(dc,
		      [this, &dc, &permission, createUploadNodes, downloadFromLinks,
		       &to_create, &output]
		      (std::unique_ptr<WorkspaceDBQuery> qobj_ptr) 
			  {
			      WorkspaceDBQuery &qobj = *qobj_ptr;
			      for (auto tc: to_create)
			      {
				  json::value val;
				  process_create(qobj, dc, tc, val,
						 permission, createUploadNodes, downloadFromLinks);
				  output.emplace_back(val);
			      }
			  });

    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

/** 
 * Handle the creation of a single workspace object.
 *
 * Parse the path, ensuring valid workspace is defined.
 * If no workspace exists, we are creating a workspace
 * Validate object type.
 * Check for overwrites.
 */
void WorkspaceService::process_create(WorkspaceDBQuery & qobj, DispatchContext &dc,
				      ObjectToCreate &to_create, boost::json::value &ret_value,
				      const std::string &permission, bool createUploadNodes, bool downloadFromLinks)
{
    std::cerr << "Create: " << to_create << "\n";
    to_create.parsed_path = qobj.parse_path(to_create.path);
    std::cerr << "Parsed: " << to_create.parsed_path << "\n";

    // Check the parsed path to see if the workspace is valid.

    std::cerr << "process create running in thread " << std::this_thread::get_id() << "\n";

    WSWorkspace &ws = to_create.parsed_path.workspace;

    // Validate creation of new workspace
    if (ws.name.empty() || ws.owner.empty())
    {
	// WS has to be named.
	BOOST_LOG_SEV(dc.lg_, wslog::debug) << "no workspace name";
	ret_value.emplace_array();
	return;
    }
    if (ws.uuid.empty())
    {
	// Workspace does not exist; check permissions for creation

	if (ws.owner != dc.token.user() && !dc.admin_mode)
	{
	    // Can't create WS owned by other user
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "WS owned by other user";
	    ret_value.emplace_array();
	    return;
	}
	else if (!is_folder(to_create.type))
	{
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "WS creation has to be of folder type";
	    ret_value.emplace_array();
	    return;
	}
	else if (!ws.has_valid_name())
	{
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "WS has invalid name";
	    ret_value.emplace_array();
	    return;
	}
	std::cerr << "create go for ws portion " << ws << "\n";
    }
    

    // If we are just requesting the creation of a workspace, we are done.
    if (to_create.parsed_path.is_workspace_path())
    {
	{
	    // WS already exists; we can just return.
	    ret_value = qobj.lookup_object_meta(to_create.parsed_path).serialize();
	    return;
	}
    }
    else
    {
	// Validation tests for creation of object
	
	if (!qobj.user_has_permission(ws, WSPermission::write))
	{
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "permission denied on create";
	    ret_value.emplace_array();
	    return;
	}
	if (!to_create.parsed_path.has_valid_name())
	{
	    // Check, but this shouldn't be possible due to the parsing rules.
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "Name has invalid characters";
	    ret_value.emplace_array();
	    return;
	}
	// Look up object to check for overwrite.
	ObjectMeta meta = qobj.lookup_object_meta(to_create.parsed_path);
	std::cerr << "existing obj: " << meta << "\n";
	    

	if (meta.valid)
	{
	    // Object exists. Check to see if it is a folder and if we are creating a folder
	    if (meta.is_folder())
	    {
		if (to_create.type == "folder" || to_create.type == "model_folder")
		{
		    // Just return data.
		    ret_value = meta.serialize();
		    return;
		}
		else
		{
		    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "Cannot overwrite folder with object";
		    ret_value.emplace_array();
		    return;
		}
	    }
	    else
	    {
		if (to_create.type == "folder" || to_create.type == "model_folder")
		{
		    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "Cannot overwrite object with folder";
		    ret_value.emplace_array();
		    return;
		}
	    }

	    // We are overwriting this object. 
	}
	else
	{
	    // We are creating a new object.
	}
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
	    dc.admin_mode = shared_state_.config().user_is_admin(dc.token.user());
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
    db_.run_in_thread(dc,
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
	    dc.admin_mode = shared_state_.config().user_is_admin(dc.token.user());

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
	db_.run_in_thread(dc,
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
		    const std::string &fs_path = shared_state_.config().filesystem_path_for_object(path);
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
		    AuthToken &token = shared_state_.ws_auth(dc.yield);
		    std::cerr << "got ws auth " << token << "\n";
		    shared_state_.shock().acl_add_user(meta.shockurl, token, dc.yield);
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
	    dc.admin_mode = shared_state_.config().user_is_admin(dc.token.user());

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

    db_.run_in_thread(dc,
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

    AuthToken &ws_auth = shared_state_.ws_auth(dc.yield);
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
		    output.emplace_back(shared_state_.config().download_url_base() + "/" + key + "/" + encoded_name);
		}
		    
	    }
	};

    db_.run_in_thread(dc, work);

    for (auto url: shock_urls)
    {
	shared_state_.shock().acl_add_user(url, dc.token, dc.yield);
    }

    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

