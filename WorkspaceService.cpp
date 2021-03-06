#include "WorkspaceService.h"
#include "WorkspaceDB.h"
#include "WorkspaceConfig.h"
#include "WorkspaceState.h"

#include <experimental/map>

#include <boost/bind/bind.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace json = boost::json;
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
namespace fs = boost::filesystem;

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

/**
 * Push an ObjectMeta with just an error field to the given output array.
 */
static void push_error_meta(json::array &output, const std::string &err)
{
    output.emplace_back(ObjectMeta(ObjectMeta::errorMeta, err).serialize());
}

WorkspaceService::WorkspaceService(boost::asio::io_context &ioc, boost::asio::ssl::context &ssl_ctx,
				   WorkspaceDB &db, WorkspaceState &state)
    : wslog::LoggerBase("wssvc")
    , ioc_(ioc)
    , db_(db)
    , shared_state_(state)
    , ssl_ctx_(ssl_ctx)
    , shock_ioc_{}
    , timer_(shock_ioc_)
    , shock_(shock_ioc_, ssl_ctx, state.config().shock_server()) {
    
    init_dispatch();

    /*
     * Start our Shock processing thread. We have a private io_context
     * we use to serialize access to the Shock service for the purpose
     * of atomically (from the WS service point of view) updating and querying
     * metadata.
     */

    shock_thread_ = std::thread([this]() {
	std::cerr << "starting WS shock thread " << std::this_thread::get_id() << "\n";
	net::executor_work_guard<net::io_context::executor_type> guard
	    = net::make_work_guard(shock_ioc_);
	shock_ioc_.run(); 

	std::cerr << "exiting shock thread " << std::this_thread::get_id() << "\n";
    });

    /*
     * Spawn a coroutine to manage the WS objects for which we
     * need Shock metadata updates.
     */

    boost::asio::spawn(shock_ioc_, boost::bind(&WorkspaceService::run_timer, this, boost::placeholders::_1));
}


void WorkspaceService::run_timer(boost::asio::yield_context yield)
{
    boost::system::error_code ec;
    timer_.expires_from_now(boost::posix_time::seconds(5), ec);

    if (ec)
    {
	std::cerr << "expires_from_now failed: " << ec.message() << "\n";
	return;
    }
    while (1)
    {
	timer_.async_wait(yield[ec]);
	if (ec)
	{
	    std::cerr << "wait failed: " << ec.message() << "\n";
	    return;
	}
	int n_updated = 0;
	for (auto &up: pending_uploads_)
	{
	    check_pending_upload(up.second, yield);
	    if (up.second.updated())
		n_updated++;
	}

	/*
	 * If we have any updated, hand them to the database to
	 * update the size.
	 * We create a DispatchContext here using the local shock io_context.
	 */
	if (n_updated > 0)
	{
	    DispatchContext dc{yield, shock_ioc_, AuthToken(), lg_};
	    db_.run_in_sync_thread(dc,
				   [this, &dc]
				   (std::unique_ptr<WorkspaceDBQuery> qobj_ptr) 
				       {
					   WorkspaceDBQuery &qobj = *qobj_ptr;

					   for (auto iter = pending_uploads_.begin(); iter != pending_uploads_.end(); ++iter)
					   {
					       auto &up = iter->second;
					       if (up.updated())
					       {
						   qobj.set_object_size(up.object_id(), up.size());
					       }
					   }
				       });

	    // And erase the updated entries.
	    std::experimental::erase_if(pending_uploads_, [](const auto& item) {
		return item.second.updated();
	    });
	}
	
	timer_.expires_from_now(boost::posix_time::seconds(5), ec);
	if (ec) {
	    std::cerr << "expires_from_now failed: " << ec.message() << "\n";
	    return;
	}
    }
}

void WorkspaceService::check_pending_upload(PendingUpload &p, net::yield_context yield)
{
    std::cerr << "Checking pending " << p << "\n";
    json::object node = shock_.get_node(p.auth_token(), p.shock_url(), yield);
    std::cerr << "shock got " << node << "\n";

    try {
	auto file = node["file"].as_object();
	size_t size = file["size"].as_int64();
	auto ck = file["checksum"].as_object();
	auto iter = ck.find("md5");
	if (iter != ck.end())
	{
	    // If there is a checksum, the file was uploaded.
	    // Need to check here in case the file is a zero-length file.
	    p.set_size(size);
	    std::cerr << "found checksum " << iter->value().as_string() << "\n";
	}
    } catch (std::invalid_argument e) {
	std::cerr << "error extracting node id: " << e.what() << "\n";
    }
    std::cerr << "Done checking pending " << p << "\n";
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
    std::string permission, owner;
    auto &cfg = shared_state_.config();
    bool createUploadNodes, downloadFromLinks, overwrite;
    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();
	createUploadNodes = object_at_as_bool(input, "createUploadNodes");
	downloadFromLinks = object_at_as_bool(input, "downloadFromLinks");
	overwrite = object_at_as_bool(input, "overwrite");
	permission = object_at_as_string(input, "permission", "n");
	
	if (object_at_as_bool(input, "adminmode"))
	    dc.admin_mode = cfg.user_is_admin(dc.token.user());

	/*
	 * Determine owner of new objects. If we have setowner passed in,
	 * we have to be an admin. If not an admin, flag error.
	 * Otherwise owner is set to the setowner value.
	 *
	 * If setowner not set, the owner is the user present on the token.
	 * This is an authentication-required method so we will have a token.
	 */
       
	auto o = input.find("setowner");
	if (o != input.end())
	{
	    if (!dc.admin_mode)
	    {
		BOOST_LOG_SEV(dc.lg_, wslog::debug) << "setowner without admin mode";
		resp.set_error(-32602, "Setowner requested without valid admin");
		http_code = 500;
		return;
	    }
	    owner = o->value().as_string().c_str();
	}
	else
	{
	    owner = dc.token.user();
	}
		
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
	    ObjectToCreate tc{obj};
	    std::string canon;
	    if (!cfg.is_valid_type(tc.type, canon))
	    {
		BOOST_LOG_SEV(dc.lg_, wslog::debug) << "Invalid type requested " << tc.type;
		resp.set_error(-32602, "Invalid object type requested");
		http_code = 500;
		return;
	    }
	    tc.type = canon;

	    /*
	     * If creation time wasn't specified, use the current time.
	     */
	    if (is_empty_time(tc.creation_time))
		tc.creation_time = current_time();

	    /*
	     * Assign an ID. We do this here so that we can tag the
	     * generated Shock node with the associated workspace object.
	     */
	    boost::uuids::uuid uuidobj = (*(db_.uuidgen()))();
	    tc.uuid = boost::lexical_cast<std::string>(uuidobj);

	    to_create.emplace_back(tc);
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

    
    RemovalRequest remreq;

    json::array output;
    db_.run_in_sync_thread(dc,
			   [this, &dc, &permission,
			    createUploadNodes, downloadFromLinks, &owner, overwrite,
			    &remreq, &to_create, &output]
			   (std::unique_ptr<WorkspaceDBQuery> qobj_ptr) 
			       {
				   WorkspaceDBQuery &qobj = *qobj_ptr;
				   for (auto tc: to_create)
				   {
				       json::value val;
				       process_create(qobj, dc, tc, val,
						      permission, createUploadNodes, downloadFromLinks, overwrite, owner, remreq);
				       output.emplace_back(val);
				   }
			       });

    std::cerr << "To remove after create: " << remreq;
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
				      const std::string &permission, bool createUploadNodes, bool downloadFromLinks,
				      bool overwrite, const std::string &owner, RemovalRequest &remreq)
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
	auto &a = ret_value.emplace_array();
	a = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "no workspace name" };
	std::cerr << "created err " << ret_value << "\n";
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
	std::string ws_uuid = qobj.create_workspace(to_create);
	if (ws_uuid.empty())
	{
	    BOOST_LOG_SEV(dc.lg_, wslog::error) << "Error creating workspace";
	    ret_value.emplace_array();
	    return;
	}
	ws.uuid = ws_uuid;
	ws.creation_time = to_create.creation_time;
    }

    // If we are just requesting the creation of a workspace, we are done.
    if (to_create.parsed_path.is_workspace_path())
    {
	ret_value = qobj.lookup_object_meta(to_create.parsed_path).serialize();
	return;
    }

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

    bool overwriteObject = false;

    /*
     * We might have to create intermediate path components; keep
     * a list of them here.
     */
    std::vector<ObjectToCreate> intermediates;

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

	if (!overwrite)
	{
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "Object exists but overwrite was not specified";
	    ret_value.emplace_array();
	    return;
	}

	overwrite = true;
    }
    else
    {
	/*
	 * Object does not exist.
	 * Validate that the workspace objects along the path to this object
	 * already exist.
	 */

	std::cerr << "check for intermediates: " << to_create << "\n";
	
	std::vector<std::string> path_comps = to_create.path_components();
	WSPath qpath(to_create.parsed_path);
	qpath.path = "";

	auto iter = path_comps.begin();
	while (iter != path_comps.end())
	{
	    qpath.name = *iter;
	    ObjectMeta pmeta = qobj.lookup_object_meta(qpath);
	    if (pmeta.valid)
	    {
		/*
		 * If we have an intermediate object that is not a folder
		 * fail.
		 */
		if (!pmeta.is_folder())
		{
		    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "Intermediate path object is not a folder";
		    ret_value.emplace_array();
		    return;
		}
	    }
	    else
	    {
		ObjectToCreate ptc(qpath, "folder");
		boost::uuids::uuid uuidobj = (*(db_.uuidgen()))();
		ptc.uuid = boost::lexical_cast<std::string>(uuidobj);
		intermediates.push_back(ptc);
	    }
	    
	    if (!qpath.path.empty())
		qpath.path += "/";
	    qpath.path += qpath.name;
	    ++iter;
	}
    }

    /*
     * At this point we've validated our request and know that it is an
     * ordinary object that needs to be created, possibly with a Shock node
     * (based on the value of createUploadNodes)
     *
     */

    if (createUploadNodes)
    {
	/*
	 * We spawn a coroutine to perform the Shock operations
	 */

	boost::system::error_code ec;
	net::io_context &ioc = shared_state_.ioc();
	
	/*
	 * We use a mutex to block the calling thread until the asyncronous operations all complete.
	 */
	std::mutex lock;
	lock.lock();
	std::string node_id;
	boost::asio::spawn(ioc, [&node_id, &dc, &lock, &to_create, this] (net::yield_context yield)
	    {
		// Get the auth token for the owner of the Shock nodes if needed.
		AuthToken &token = shared_state_.ws_auth(yield);
		
		node_id = shared_state_.shock().create_node(token, to_create.uuid, yield);
		std::cerr << "created node " << node_id << "\n";
		if (node_id.empty())
		{
		    lock.unlock();
		    return;
		}
		to_create.shock_node = shared_state_.config().shock_server() + "/node/" + node_id;
		shared_state_.shock().acl_add_user(to_create.shock_node, token, dc.token.user(), yield);
		boost::asio::post(shock_ioc_, [&lock, this, to_create, dtoken = dc.token ] () {
		    pending_uploads_.emplace(std::make_pair(to_create.uuid, PendingUpload{to_create.uuid, to_create.shock_node, dtoken}));
		    lock.unlock();
		});
	    });
	lock.lock();
	lock.unlock();
	if (node_id.empty())
	{
	    BOOST_LOG_SEV(dc.lg_, wslog::error) << "Error creating shock node";
	    ret_value.emplace_array();
	    return;
	}
    }

    /*
     * If we are overwriting, we need to delete the existing object.
     */
    if (overwrite)
    {
	qobj.remove_workspace_object(meta, remreq);
    }
     

    // We are creating a new object.

    for (auto int_obj: intermediates)
    {
	ObjectMeta int_meta = qobj.create_workspace_object(int_obj, owner);
	std::cerr << "Created intermediate " << int_meta << "\n";
    }
	    
    ObjectMeta created = qobj.create_workspace_object(to_create, owner);
    ret_value = created.serialize();
    return;
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
		    const boost::filesystem::path &fs_path = shared_state_.config().filesystem_path_for_object(path);
		    std::cerr << "retrieve data from " << fs_path << "\n";
		    boost::filesystem::ifstream f(fs_path);
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
		    if (dc.token.valid())
			shared_state_.shock().acl_add_user(meta.shockurl, token, dc.token.user(), dc.yield);
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

    std::string username = dc.token.valid() ? dc.token.user() : ws_auth.user();

    for (auto url: shock_urls)
    {
	shared_state_.shock().acl_add_user(url, ws_auth.token(), username, dc.yield);
    }

    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

void WorkspaceService::method_update_auto_meta(const JsonRpcRequest &req, JsonRpcResponse &resp,
					       DispatchContext &dc, int &http_code)
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

    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "method_update_auto_meta  adminmode=" << dc.admin_mode << "\n";

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

	db_.run_in_sync_thread(dc,
			  [&path, path_str = obj.as_string(), &output, &meta, this]
			  (std::unique_ptr<WorkspaceDBQuery> qobj) 
			      {
				  path = qobj->parse_path(path_str);
				  if (qobj->user_has_permission(path.workspace, WSPermission::read))
				  {
				      meta = qobj->lookup_object_meta(path);

				      /*
				       * Spawn a coroutine into the thread that we're using
				       * to manage the pending uploads list. There, find the
				       * item in the pending_uploads_ list that corresponds
				       * to this item, and trigger a check. Since presumably the
				       * client invoking this is the one that did the upload, we
				       * should see the upload complete but don't worry if it doesn't
				       * since we the upload check timer may have triggered already.
				       */

				      std::mutex lock;
				      lock.lock();
				      size_t size{};
				      bool updated = false;
				      boost::asio::spawn(shock_ioc_, [&lock, &meta, &size, &updated, this](net::yield_context yield)
					  {
					      auto iter = pending_uploads_.find(meta.id);
					      if (iter == pending_uploads_.end())
					      {
						  std::cerr << "did not find pending for object\n";
						  lock.unlock();
						  return;
					      }
					      auto &pu = iter->second;
					      check_pending_upload(pu, yield);
					      if (pu.updated())
					      {
						  updated = true;
						  size = pu.size();
						  pending_uploads_.erase(iter);
					      }
					      lock.unlock();
					  });
					  
				      lock.lock();
				      lock.unlock();
				      std::cerr << "updated=" << updated << " size=" << size << "\n";
				      if (updated)
				      {
					  qobj->set_object_size(meta.id, size);
					  meta = qobj->lookup_object_meta(path);
				      }
				  }
				  output.emplace_back(meta.serialize());
			      });
    }
    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

void WorkspaceService::method_update_metadata(const JsonRpcRequest &req, JsonRpcResponse &resp,
					      DispatchContext &dc, int &http_code)
{
    json::array objects;
    bool append;

    auto &cfg = shared_state_.config();

    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();

	append = object_at_as_bool(input, "append");
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

    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "method_update_metadata  adminmode=" << dc.admin_mode << "\n";

    std::vector<ObjectToModify> to_modify;
    
    // Validate format of paths before doing any work
    for (auto obj: objects)
    {
	try {
	    ObjectToModify tc{obj};
	    if (tc.type)
	    {
		std::string canon;
		if (!cfg.is_valid_type(tc.type.value(), canon))
		{
		    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "Invalid type requested " << tc.type.value();
		    resp.set_error(-32602, "Invalid object type requested");
		    http_code = 500;
		    return;
		}
		tc.type = canon;
	    }
	    std::cerr << "TO modify: " << tc << "\n";
	    to_modify.emplace_back(tc);
	} catch (std::exception e) {
	    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "error parsing: " << e.what() << "\n";
	    resp.set_error(-32602, "Invalid request parameters");
	    http_code = 500;
	    return;
	}
    }

    json::array output;

    db_.run_in_sync_thread(dc, [&to_modify, &output, append, this]
	(std::unique_ptr<WorkspaceDBQuery> qobj) 
	{
	    for (auto obj: to_modify)
	    {
		obj.parsed_path = qobj->parse_path(obj.path);
		
		if (qobj->user_has_permission(obj.parsed_path.workspace, WSPermission::write))
		{
		    // Validate setting type on workspace.
		    // We also want to validate changing type to/from folder
		    // but defer that to where we've already looked up metadata.
		    if (obj.parsed_path.is_workspace_path())
		    {
			if (obj.type)
			{
			    std::cerr << " cannot set type on workspace\n";
			    output.emplace_back(json::array{});
			    continue;
			}
		    }

		    std::cerr << "Apply metadata change " << obj << "\n";

		    auto meta = qobj->update_object(obj, append);

		    output.emplace_back(meta.serialize());
		}
	    }
	});
    
    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

void WorkspaceService::method_set_permissions(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code)
{
    std::string path;
    std::vector<UserPermission> user_permissions;
    std::experimental::optional<WSPermission> new_global_permission;
    
    try {
	auto input = req.params().at(0).as_object();
	path = input.at("path").as_string().c_str();
	auto perm_it = input.find("permissions");
	if (perm_it != input.end() && perm_it->value().kind() == json::kind::array)
	{
	    for (auto p: perm_it->value().as_array())
	    {
		user_permissions.emplace_back(p);
	    }
	}
	auto glob_it = input.find("new_global_permission");
	if (glob_it != input.end() &&
	    glob_it->value().kind() == json::kind::string)
	{
	    new_global_permission.emplace(to_permission(glob_it->value().as_string().c_str()));
	}

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

    json::array output;

    /*
     * Perform the requested operations.
     *
     * Verify the path is a workspace path (permissions are only set on workspaces).
     * Verify user permissions.
     *    If the workspace is a published workspace, only allow owner or admin to change perms.
     *    Otherwise check for admin permission on the workspace.
     * Verify no user-specfic perms are "publish"
     * If new_global_permission is set
     *    If new global permission is publish, verify user is owner
     *    Update global perm
     * For each user perm,
     *    If new perm is "none", delete existing permission for that user
     *    Otherwise set permission for that user
     * Construct output for new current state.
     */

    std::experimental::optional<std::string> err;
    db_.run_in_sync_thread(dc, [&path, &user_permissions, &new_global_permission, &output, &err, this]
	(std::unique_ptr<WorkspaceDBQuery> qobj) 
	{
	    err = qobj->update_permissions(path, user_permissions, new_global_permission, output);
	});


    if (err)
    {
	std::cerr << "error from update: " << *err << "\n";
    }
    
    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";

}


void WorkspaceService::method_copy(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code)
{
    bool overwrite;
    bool recursive;
    bool move;
    std::vector<std::pair<std::string, std::string>> objects;

    auto &cfg = shared_state_.config();

    try {
	auto input = req.params().at(0).as_object();
	auto objs = input.at("objects").as_array();
	for (auto obj: objs)
	{
	    auto lobj = obj.as_array();
	    objects.emplace_back(lobj.at(0).as_string().c_str(), lobj.at(1).as_string().c_str());
	}

	overwrite = object_at_as_bool(input, "overwrite");
	recursive = object_at_as_bool(input, "recursive");
	move = object_at_as_bool(input, "move");
	
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

    BOOST_LOG_SEV(dc.lg_, wslog::debug) << "method_copy  adminmode=" << dc.admin_mode << "\n";

    json::array output;


    if (move)
    {
	db_.run_in_sync_thread(dc, [&objects, &overwrite, &output, this]
			       (std::unique_ptr<WorkspaceDBQuery> qobj) 
	    {
		for (auto &obj: objects)
		{
		    std::string &from = obj.first;
		    std::string &to = obj.second;

		    ObjectMeta meta = qobj->perform_move(from, to, overwrite);
		    output.emplace_back(meta.serialize());
		    
		}
	    });
    }
    else
    {
	db_.run_in_sync_thread(dc, [&objects, &overwrite, &recursive, &output, this]
			       (std::unique_ptr<WorkspaceDBQuery> qobj) 
	    {
		for (auto &obj: objects)
		{
		    std::string &from = obj.first;
		    std::string &to = obj.second;

		    ObjectMeta meta = qobj->perform_copy(from, to, recursive, overwrite);
		    std::cerr << from << " " << to << " got " << meta << "\n";
		    output.emplace_back(meta.serialize());
		    std::cerr << "output now " << output << "\n";
		}
	    });
    }


    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}

void WorkspaceService::method_delete(const JsonRpcRequest &req, JsonRpcResponse &resp,
				     DispatchContext &dc, int &http_code)
{
    json::array objects;
    auto &cfg = shared_state_.config();
    bool delete_directories, force;
    try {
	auto input = req.params().at(0).as_object();
	objects = input.at("objects").as_array();
	delete_directories = object_at_as_bool(input, "deleteDirectories");
	force = object_at_as_bool(input, "force");
	
	if (object_at_as_bool(input, "adminmode"))
	    dc.admin_mode = cfg.user_is_admin(dc.token.user());
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

    std::vector<fs::path> paths_to_remove;
    std::vector<ObjectMeta> shock_nodes_to_remove;
    
    json::array output;

    RemovalRequest remreq;

    db_.run_in_thread(dc,
		      [this, delete_directories, force, &objects, &paths_to_remove, &shock_nodes_to_remove, &output, &remreq]
		      (std::unique_ptr<WorkspaceDBQuery> qobj_ptr) 
			  {
			      WorkspaceDBQuery &qobj = *qobj_ptr;
			      for (auto obj: objects)
			      {
				  WSPath path = qobj.parse_path(obj);
				  
				  if (!qobj.user_has_permission(path.workspace, WSPermission::write))
				  {
				      BOOST_LOG_SEV(lg_, wslog::debug) << "no permission to remove " << path;
				      push_error_meta(output, "Permission denied to remove object");
				      continue;
				  }
				  
				  ObjectMeta meta = qobj.lookup_object_meta(path);
				  
				  if (!meta.valid)
				  {
				      push_error_meta(output, "Object does not exist");
				      continue;
				  }

				  /*
				   * Validate. If the object is a folder, the action taken
				   * depends on the values of delete_directories and force.
				   * delete_directories=true, force=true: recursive delete
				   * delete_directories=true, force=false: remove folder only if it is empty
				   * delete_directories=false: error
				   *
				   * If the object is not a folder, just remove.
				   */

				  bool removed;
				  if (meta.is_folder())
				  {
				      if (!delete_directories)
				      {
					  push_error_meta(output, "Trying to delete folder but deleteDirectories not set");
					  continue;
				      }
				      if (force)
				      {
					  removed = qobj.remove_workspace_folder_and_contents(meta, remreq);
				      }
				      else
				      {
					  removed = qobj.remove_workspace_folder_only(meta, remreq);
				      }
				  }
				  else
				  {
				      std::cerr << "not removing " << meta << "\n";
//				      removed = qobj.remove_workspace_object(meta, remreq);
				  }

				  if (removed)
				  {
				      std::cerr << "Deleted " << meta << "\n";
				  }
				  else
				  {
				      std::cerr << "Did not delete " << meta << "\n";
				  }
				  
				  
				  output.emplace_back(meta.serialize());
			      }
			  });

    std::cerr << "To remove after delete: " << remreq;

    resp.result().emplace_back(output);
    BOOST_LOG_SEV(dc.lg_, wslog::debug) << output << "\n";
}
