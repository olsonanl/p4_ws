#include "WorkspaceDB.h"
#include "PathParser.h"
#include "parse_url.h"
#include "WorkspaceConfig.h"
#include "WorkspaceTypes.h"

#include <mongocxx/collection.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/client.hpp>
#include <bsoncxx/json.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

#include <boost/asio.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/types.hpp>

// namespace beast = boost::beast;         // from <boost/beast.hpp>
// namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
// using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace fs = boost::filesystem;

using bsoncxx::builder::basic::kvp;
namespace builder = bsoncxx::builder;

//
// URL decoder borrowed from Boost
// boost_1_73_0/doc/html/boost_asio/example/cpp03/http/server4/file_handler.cpp
//

inline std::string mongo_user_encode(const std::string& in)
{
    std::ostringstream os;
    for (std::size_t i = 0; i < in.size(); ++i)
    {
	switch (in[i])
	{
	case '.':
	    os << "%2E";
	    break;
	case '$':
	    os << "%24";
	    break;
	default:
	    os << in[i];
	}
    }
    return std::move(os.str());
}

static std::string get_stringish(const bsoncxx::document::element &elt)
{
    long lval {};
    int ival {};
    switch (elt.type())
    {
    case bsoncxx::type::k_utf8:
	return static_cast<std::string>(elt.get_utf8().value);

    case bsoncxx::type::k_int64:
	lval = elt.get_int64().value;
	return std::to_string(lval);

    case bsoncxx::type::k_int32:
	ival = elt.get_int32().value;
	return std::to_string(ival);
    }

    return "";
}


static std::string get_string(const bsoncxx::document::view &doc, const std::string &key)
{
    std::string v;
    auto iter = doc.find(key);
    if (iter != doc.end())
    {
	auto val = *iter;
	if (val.type() == bsoncxx::type::k_utf8)
	{
	    v = static_cast<std::string>(val.get_utf8().value);
	}
    }
    return v;

}

static std::map<std::string, std::string> get_map(const bsoncxx::document::view &doc, const std::string &key)
{
    std::map<std::string, std::string> map;

    auto iter = doc.find(key);
    if (iter == doc.end())
	return map;
	    
    const bsoncxx::document::element &val = *iter;

    if (val.type() == bsoncxx::type::k_document)
    {
	for (auto elt: val.get_document().value)
	{
	    auto key = elt.key();
	    auto kval = get_stringish(elt);
	    map.insert(std::make_pair(key, kval));
	}
    }
    return map;
}

static std::map<std::string, WSPermission> get_perm_map(const bsoncxx::document::view &doc, const std::string &key)
{
    std::map<std::string, WSPermission> map;

    auto iter = doc.find(key);
    if (iter == doc.end())
	return map;
	    
    const bsoncxx::document::element &val = *iter;

    if (val.type() == bsoncxx::type::k_document)
    {
	for (auto elt: val.get_document().value)
	{
	    auto key = elt.key();
	    auto kval = get_stringish(elt);
	    map.insert(std::make_pair(key, to_permission(kval)));
	}
    }
    return map;
}

/*
 * it really gets an int32 too
 */
static long get_int64(const bsoncxx::document::view &doc, const std::string &key)
{
    long v = {};

    auto iter = doc.find(key);
    if (iter == doc.end())
	return v;
	    
    const bsoncxx::document::element &val = *iter;

    if (val.type() == bsoncxx::type::k_int64)
	v = static_cast<long>(val.get_int64().value);
    else if (val.type() == bsoncxx::type::k_int32)
	v = static_cast<long>(val.get_int32().value);

    return v;
}

static std::tm get_tm(const bsoncxx::document::view &doc, const std::string &key)
{
    std::tm v = {};

    auto iter = doc.find(key);
    if (iter == doc.end())
	return v;
	    
    const bsoncxx::document::element &val = *iter;

    if (val.type() == bsoncxx::type::k_utf8)
    {
	std::string str = static_cast<std::string>(val.get_utf8().value);
	std::istringstream ss(str);
	ss >> std::get_time(&v, "%Y-%m-%dT%H:%M:%SZ");
    }
    return v;
}

bool WorkspaceDB::init_database(const std::string &uri, int threads, const std::string &db_name)
{
    db_name_ = db_name;
    uri_ = static_cast<mongocxx::uri>(uri);
    pool_ = std::make_unique<mongocxx::pool>(uri_);
    n_threads_ = threads;
    thread_pool_ = std::make_unique<boost::asio::thread_pool>(threads);

    // Start up our sync thread
    sync_thread_ = std::thread([this]() {
	net::executor_work_guard<net::io_context::executor_type> guard
	    = net::make_work_guard(sync_ioc_);
	sync_ioc_.run(); 
    });
       
    return true;
}

/*
 * Query needs to post into the thread pool,
 * passing the yield context. 
 */

std::unique_ptr<WorkspaceDBQuery> WorkspaceDB::make_query(const AuthToken &token, bool admin_mode)
{
    auto pe = pool_->acquire();
    return std::make_unique<WorkspaceDBQuery>(token, admin_mode, std::move(pe), *this);
}

inline std::string to_string(boost::json::string s)
{
    return std::string(s.c_str(), s.size());
}

mongocxx::collection WorkspaceDBQuery::object_collection()
{
    return (*client_)[db_.db_name()]["objects"];
}

mongocxx::collection WorkspaceDBQuery::download_collection()
{
    return (*client_)[db_.db_name()]["downloads"];
}

mongocxx::collection WorkspaceDBQuery::workspace_collection()
{
    return (*client_)[db_.db_name()]["workspaces"];
}

ObjectMeta WorkspaceDBQuery::lookup_object_meta(const WSPath &o)
{
    ObjectMeta meta;

    /*
     * If name is empty, return metadata for the workspace.
     * Otherwise return metadata for the object.
     * If object does not exist, return empty metadata object.
     */

    if (o.name.empty())
    {
	meta.name = o.workspace.name;
	meta.type = "folder";
	meta.path = "/" + o.workspace.owner + "/";
	meta.creation_time = o.workspace.creation_time;
	meta.id = o.workspace.uuid;
	meta.owner = o.workspace.owner;
	meta.size = 0;
	meta.user_metadata = o.workspace.metadata;
	meta.global_permission = o.workspace.global_permission;
	meta.user_permission = effective_permission(o.workspace);
	meta.valid = true;
    }
    else
    {
	auto coll = object_collection();
	auto qry = builder::basic::document{};

	qry.append(kvp("path", o.path));
	qry.append(kvp("name", o.name));
	qry.append(kvp("workspace_uuid", o.workspace.uuid));

	BOOST_LOG_SEV(lg_, wslog::debug) << "qry: " << bsoncxx::to_json(qry.view()) << "\n";
	auto cursor = coll.find(qry.view());

	auto ent = cursor.begin();
	if (ent == cursor.end())
	{
	    BOOST_LOG_SEV(lg_, wslog::debug) << "Not found\n";
	    return meta;
	}
	auto &obj = *ent;
	
	meta = metadata_from_db(o.workspace, obj);
    }

    return meta;
}

ObjectMeta WorkspaceDBQuery::metadata_from_db(const WSWorkspace &ws, const bsoncxx::document::view &obj)
{
    ObjectMeta meta;

    meta.name = get_string(obj, "name");
    meta.type = get_string(obj, "type");

    auto path = get_string(obj, "path");

    meta.ws_path.workspace = ws;
    meta.ws_path.path = path;
    meta.ws_path.name = meta.name;
    meta.ws_path.empty = 0;

    meta.path = "/" + ws.owner + "/" + ws.name + "/" + path;
    if (meta.path[meta.path.size()-1] != '/')
	meta.path += "/";
    meta.creation_time = get_tm(obj, "creation_date");
    meta.id = get_string(obj, "uuid");
    meta.owner = get_string(obj, "owner");
    meta.size = get_int64(obj, "size");
    meta.user_metadata = get_map(obj, "metadata");
    meta.auto_metadata = get_map(obj, "autometadata");
    meta.user_permission = effective_permission(ws);
    meta.global_permission = ws.global_permission;
    if (get_int64(obj, "shock"))
    {
	meta.shockurl = get_string(obj, "shocknode");
    }
    meta.valid = true;
    return meta;
}

void WorkspaceDBQuery::populate_workspace_from_db_obj(WSWorkspace &ws, const bsoncxx::document::view &obj)
{
    auto uuid = obj["uuid"];
    auto cdate = obj["creation_date"];
    auto perm = obj["global_permission"];
    auto all_perms = get_perm_map(obj, "permissions");

    ws.uuid = static_cast<std::string>(uuid.get_utf8().value);
    ws.global_permission = to_permission(perm.get_utf8().value[0]);

    if (ws.owner.empty())
	ws.owner = static_cast<std::string>(obj["owner"].get_utf8().value);
    if (ws.name.empty())
	ws.name = static_cast<std::string>(obj["name"].get_utf8().value);

    /* parse time. */

    std::string creation_time(static_cast<std::string>(cdate.get_utf8().value));
    std::istringstream ss(creation_time);
    ss >> std::get_time(&ws.creation_time, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail())
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "creation date parse failed '" << creation_time << "'\n";
    }

    for (auto p: all_perms)
    {
	// Key here is the user id, url-encoded.
	std::string user;
	if (url_decode(p.first, user))
	{
	    ws.user_permission.insert(std::make_pair(user, p.second));
	    // BOOST_LOG_SEV(lg_, wslog::debug) << user << ": " << p.second << "\n";
	}
    }
}

void WorkspaceDBQuery::populate_workspace_from_db(WSWorkspace &ws)
{
    auto coll = workspace_collection();
    auto qry = builder::basic::document{};

    qry.append(kvp("owner", ws.owner));
    qry.append(kvp("name", ws.name));

    // BOOST_LOG_SEV(lg_, wslog::debug) << "qry: " << bsoncxx::to_json(qry.view()) << "\n";

    auto cursor = coll.find(qry.view());
    auto ent = cursor.begin();

    if (ent == cursor.end())
	return;

    auto &obj = *ent;

    populate_workspace_from_db_obj(ws, obj);

    ent++;
    if (ent != cursor.end())
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "nonunique workspace!\n";
    }
}

WSPath WorkspaceDBQuery::parse_path(const boost::json::value &pstr)
{
    if (pstr.kind() == boost::json::kind::string)
	return parse_path(pstr.as_string());
    else
	return WSPath();
}

WSPath WorkspaceDBQuery::parse_path(const boost::json::string &pstr)
{
    return parse_path(std::string(pstr.c_str()));
}

WSPath WorkspaceDBQuery::parse_path(const std::string &pstr)
{
    BOOST_LOG_SEV(lg_, wslog::debug) << "validating " << pstr << "\n";

    WSPathParser parser;
    WSPath path;

    if (parser.parse(pstr))
    {
	path = parser.extract_path();

	if (!path.workspace.owner.empty() && !path.workspace.name.empty())
	{
	    populate_workspace_from_db(path.workspace);
	}
	path.empty = false;
    }
    else
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "Path parse failed for '" << pstr << "'\n";
    }
    
    return path;
}

WSPermission WorkspaceDBQuery::effective_permission(const WSWorkspace &w)
{
    // BOOST_LOG_SEV(lg_, wslog::debug) << "compute permission for user " << token() << " and workspace owned by " << w.owner << "\n";

    if (!token().valid())
    {
	// BOOST_LOG_SEV(lg_, wslog::debug) << "  global fallback for invalid token\n";
	return w.global_permission;
    }

    if (w.global_permission == WSPermission::public_)
    {
	// BOOST_LOG_SEV(lg_, wslog::debug) << "  is public\n";
	return WSPermission::public_;
    }

    if (w.owner == token().user())
    {
	// BOOST_LOG_SEV(lg_, wslog::debug) << "  is owner\n";
	return WSPermission::owner;
    }

    static std::map<WSPermission, int> perm_ranks {
	{ WSPermission::none, 0 },
	{ WSPermission::invalid, 0 },
	{ WSPermission::public_, 1 },
	{ WSPermission::read, 1 },
	{ WSPermission::write, 2 },
	{ WSPermission::admin, 3 },
	{ WSPermission::owner, 4 }
    };

    if (token().valid())
    {
	auto has_perm = w.user_permission.find(token().user());
	if (has_perm != w.user_permission.end())
	{
	    WSPermission user_perm = has_perm->second;
	    int rank_user = perm_ranks[user_perm];
	    int rank_global = perm_ranks[w.global_permission];
	    
	    if (rank_user > rank_global)
	    {
		// BOOST_LOG_SEV(lg_, wslog::debug) << "  ranks " << rank_user << " " << rank_global << " determined " << user_perm << "\n";
		return user_perm;
	    }
	}
    }
    // BOOST_LOG_SEV(lg_, wslog::debug) << "  global fallback\n";
    return w.global_permission;
}

bool WorkspaceDBQuery::user_has_permission(const WSWorkspace &w, WSPermission min_permission)
{
    if (admin_mode())
	return true;

    static std::map<WSPermission, int> perm_ranks {
	{ WSPermission::none, 0 },
	{ WSPermission::invalid, 0 },
	{ WSPermission::public_, 1 },
	{ WSPermission::read, 1 },
	{ WSPermission::write, 2 },
	{ WSPermission::admin, 3 },
	{ WSPermission::owner, 4 }
    };

    int rank_user = perm_ranks[effective_permission(w)];
    int rank_needed = perm_ranks[min_permission];
    return rank_user >= rank_needed;
}

std::vector<ObjectMeta> WorkspaceDBQuery::list_objects(const WSPath &path, bool excludeDirectories, bool excludeObjects, bool recursive)
{
    auto coll = object_collection();

    auto qry = builder::basic::document{};

    qry.append(kvp("workspace_uuid", path.workspace.uuid));
    if (excludeDirectories)
	qry.append(kvp("folder", 0));
    else if (excludeObjects)
	qry.append(kvp("folder", 1));

    if (recursive)
    {
	std::string regex_str = "^" + path.full_path() + "($|/)";
        auto regex = bsoncxx::types::b_regex(regex_str);
	qry.append(kvp("path", regex));
    }
    else
    {
	qry.append(kvp("path", path.full_path()));
    }
    auto cursor = coll.find(qry.view());

    std::vector<ObjectMeta> meta_list;
    for (auto ent = cursor.begin(); ent != cursor.end(); ent++)
    {
	auto &obj = *ent;
	
	meta_list.emplace_back(metadata_from_db(path.workspace, obj));
    }
    return meta_list;
}

/*
 * list workspace query looks something like this
 *  { $or : [ {"permissions.olson@patricbrc%2Eorg": { $exists : 1 } }, { "global_permission": { $ne: "n" }}, { "owner": "olson@patricbrc.org"}]}
 */

std::vector<ObjectMeta> WorkspaceDBQuery::list_workspaces(const std::string &owner)
{
    auto coll = workspace_collection();

    std::vector<ObjectMeta> meta_list;

    builder::stream::array terms;
    builder::stream::document is_owner;

    if (!owner.empty())
    {
	is_owner << "owner"
		 << owner;
    }

    terms << builder::stream::open_document
	  <<   (std::string("permissions.") + mongo_user_encode(token().user()))
	  <<   builder::stream::open_document << "$exists" << bsoncxx::types::b_bool{true} << builder::stream::close_document
	  << builder::stream::close_document
	  << builder::stream::open_document
	  <<   "owner"
	  <<   token().user()
	  << builder::stream::close_document 
	  << builder::stream::open_document
	  <<   "global_permission"
	  <<   builder::stream::open_document << "$ne" << "n" << builder::stream::close_document
	  << builder::stream::close_document;

    builder::stream::document qry;

    qry << "$or" << terms
	<< builder::concatenate(is_owner.view());
    
    //BOOST_LOG_SEV(lg_, wslog::debug) << "qry: " << bsoncxx::to_json(qry.view()) << "\n";
    auto cursor = coll.find(qry.view());

    for (auto ent = cursor.begin(); ent != cursor.end(); ent++)
    {
	auto &obj = *ent;

	WSPath path;
	populate_workspace_from_db_obj(path.workspace, obj);
	meta_list.emplace_back(lookup_object_meta(path));
    }

    return meta_list;
}

std::string WorkspaceDBQuery::insert_download_for_object(const boost::json::string &path_str, const AuthToken &ws_token,
							 ObjectMeta &meta, std::vector<std::string> &shock_urls)
{
    WSPath path = parse_path(path_str);
    meta = lookup_object_meta(path);
    if (!(meta.is_object() && user_has_permission(path.workspace, WSPermission::read)))
    {
	return "";
    }
    
    boost::uuids::uuid uuid = (*(db_.uuidgen()))();
    std::string key = boost::lexical_cast<std::string>(uuid);

    builder::stream::document qry;
    auto coll = download_collection();

    std::time_t expires = time(0) + db_.config().download_lifetime();
    
    qry << "workspace_path" << path_str.c_str()
	<< "download_key" << key
	<< "expiration_time" << expires
	<< "name" << meta.name
	<< "size" << static_cast<std::int64_t>(meta.size);
    
    if (meta.shockurl.empty())
    {
	qry << "file_path" << db_.config().filesystem_path_for_object(path).native();
    }
    else
    {
	qry << "shock_node" << meta.shockurl;

	/*
	 * If we have a valid token, include that token in the download record
	 * and ensure Shock has an acl for it (we will do that work back in the
	 * main request thread, recording the list in shock_urls).
	 * If we don't, we have a public-readable path and the token
	 * we embed is the WS owner token.
	 */
	if (token().valid())
	{
	    shock_urls.emplace_back(meta.shockurl);
	    qry << "token" << token().token();
	}
	else
	{
	    qry << "token" << ws_token.token();
	}
    }
    coll.insert_one(qry.view());
    // BOOST_LOG_SEV(lg_, wslog::debug) << "QRY: " << bsoncxx::to_json(qry.view());

    return key;
}

bool WorkspaceDBQuery::lookup_download(const std::string &key, std::string &name, size_t &size,
				       std::string &shock_node, std::string &token,
				       std::string &file_path)
{
    auto coll = download_collection();
    builder::stream::document qry;
    qry << "download_key" << key;
    auto res = coll.find_one(qry.view());
    if (res)
    {
	auto obj = res->view();
	name = get_string(obj, "name");
	size = obj["size"].get_int64().value;
	shock_node = get_string(obj, "shock_node");
	token = get_string(obj, "token");
	file_path = get_string(obj, "file_path");
	return true;
    }
    else
    {
	return false;
    }
}

std::string WorkspaceDBQuery::create_workspace(const ObjectToCreate &tc)
{
    auto &cfg = db_.config();
    auto &ws = tc.parsed_path.workspace;

    // Create the top of the hierarchy on disk
    auto path = cfg.filesystem_path_for_workspace(ws);
    boost::system::error_code ec;
    fs::create_directory(path, ec);
    if (ec)
    {
	BOOST_LOG_SEV(lg_, wslog::error) << "create_direcotry " << path << " failed: " << ec.message();
	return "";
    }

    auto coll = workspace_collection();
    builder::stream::document qry;

    boost::uuids::uuid uuidobj = (*(db_.uuidgen()))();
    std::string uuid = boost::lexical_cast<std::string>(uuidobj);

    qry << "uuid" << uuid;
    qry << "creation_date" << tc.creation_time_str();
    qry << "name" << ws.name;
    qry << "owner" << ws.owner;
    qry << "global_permission" << to_string(ws.global_permission);

    builder::stream::document permissions;
    qry << "permissions" << permissions;

    bsoncxx::document::value qry_doc = qry << builder::stream::finalize;

    auto res = coll.insert_one(qry_doc.view());
    if (res)
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "Inserted  ws " << res->result().inserted_count() << " "
		  << res->inserted_id().get_oid().value.to_string() << "\n";
    }
    else
    {
	BOOST_LOG_SEV(lg_, wslog::error) << "failed to insert workspace doc: " << bsoncxx::to_json(qry_doc.view());
	uuid = "";
    }

    return uuid;
}

ObjectMeta WorkspaceDBQuery::create_workspace_object(const ObjectToCreate &tc, const std::string &owner)
{
    ObjectMeta meta;

    auto &cfg = db_.config();

    auto coll = object_collection();
    builder::stream::document qry;

    qry << "uuid" << tc.uuid;
    qry << "creation_date" << tc.creation_time_str();

    builder::stream::document user_metadata;
    builder::stream::document auto_metadata;
    for (auto x: tc.user_metadata)
	user_metadata << x.first << x.second;
    qry << "metadata" << user_metadata;
    qry << "name" << tc.parsed_path.name;
    qry << "path" << tc.parsed_path.path;
    qry << "type" << tc.type;
    qry << "owner" << owner;
    qry << "workspace_uuid" << tc.parsed_path.workspace.uuid;
    if (is_folder(tc.type))
    {
	auto path = cfg.filesystem_path_for_object(tc.parsed_path);

	qry << "size" << 0;
	qry << "shock" << 0;
	qry << "folder" << 1;
	boost::system::error_code ec;
	fs::create_directory(path, ec);
	if (ec)
	{
	    BOOST_LOG_SEV(lg_, wslog::error) << "create_direcotry " << path << " failed: " << ec.message();
	    return meta;
	}
    }
    else
    {
	qry << "folder" << 0;

	// If we don't have a shock node, write the passed-in data to a file in the fs.

	if (tc.shock_node.empty())
	{
	    qry << "shock" << 0;

	    auto path = cfg.filesystem_path_for_object(tc.parsed_path);
	    auto dir = path.parent_path();
	    if (!fs::is_directory(dir))
	    {
		BOOST_LOG_SEV(lg_, wslog::error) << "Err: leading path was not created for " << path << "\n";
	    }
	    else
	    {
		try {
		    fs::ofstream f;
		    f.exceptions(fs::ofstream::failbit);
		    f.open(path);
		    f.write(tc.object_data.data(), tc.object_data.size());
		    f.close();
		    long sz = fs::file_size(path);
		    qry << "size" << sz;
		} catch (std::ios_base::failure &e) {
		    BOOST_LOG_SEV(lg_, wslog::error) << "error writing data to " << path << ": " << e.what() << " " << std::strerror(errno);
		    return meta;
		}
	    }
	}
	else
	{
	    qry << "size" << 0;
	    qry << "shock" << 1;
	    qry << "shocknode" << tc.shock_node;
	}
	
    }
    qry << "autometadata" << auto_metadata;
    
    bsoncxx::document::value qry_doc = qry << builder::stream::finalize;

    std::cerr << bsoncxx::to_json(qry_doc.view()) << "\n";
    
    auto res = coll.insert_one(qry_doc.view());
    if (res)
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "Inserted obj " << res->result().inserted_count() << " "
		  << res->inserted_id().get_oid().value.to_string() << "\n";
	meta = lookup_object_meta(tc.parsed_path);
    }
    else
    {
	BOOST_LOG_SEV(lg_, wslog::error) << "failed to insert workspace doc: " << bsoncxx::to_json(qry_doc.view());
    }

    return meta;
}

/**
 * Perform the copy operation for a single workspace object.
 */
ObjectMeta WorkspaceDBQuery::copy_workspace_object(const ObjectMeta &from, const WSPath &to)
{
    ObjectMeta meta;

    auto &cfg = db_.config();

    auto coll = object_collection();
    builder::stream::document qry;

    qry << "uuid" << generate_uuid();
    qry << "creation_date" << from.creation_time_str();

    builder::stream::document user_metadata;
    builder::stream::document auto_metadata;
    for (auto x: meta.user_metadata)
	user_metadata << x.first << x.second;
    qry << "metadata" << user_metadata;
    qry << "name" << to.name;
    qry << "path" << to.path;
    qry << "type" << from.type;
    qry << "owner" << token().user();
    qry << "workspace_uuid" << to.workspace.uuid;
    if (is_folder(from.type))
    {
	auto path = cfg.filesystem_path_for_object(to);

	qry << "size" << 0;
	qry << "shock" << 0;
	qry << "folder" << 1;
	boost::system::error_code ec;
	fs::create_directory(path, ec);
	if (ec)
	{
	    BOOST_LOG_SEV(lg_, wslog::error) << "create_direcotry " << path << " failed: " << ec.message();
	    return meta;
	}
    }
    else
    {
	qry << "folder" << 0;

	// If we don't have a shock node, we need to copy the filesystem data.

	if (from.shockurl.empty())
	{
	    qry << "shock" << 0;

	    auto from_path = cfg.filesystem_path_for_object(from.ws_path);
	    auto to_path = cfg.filesystem_path_for_object(to);
	    auto dir = to_path.parent_path();
	    if (!fs::is_directory(dir))
	    {
		BOOST_LOG_SEV(lg_, wslog::error) << "Err: leading path was not created for " << to_path << "\n";
	    }
	    else
	    {
		try {
		    fs::copy(from_path, to_path);
		    long sz = fs::file_size(to_path);
		    qry << "size" << sz;
		} catch (std::ios_base::failure &e) {
		    BOOST_LOG_SEV(lg_, wslog::error) << "error copying " << from_path << "  to " << to_path << ": " << e.what() << " " << std::strerror(errno);
		    return meta;
		}
	    }
	}
	else
	{
	    qry << "size" << 0;
	    qry << "shock" << 1;
	    qry << "shocknode" << from.shockurl;

	    // TODO need to bump refcount in shock data
	}
	
    }
    qry << "autometadata" << auto_metadata;
    
    bsoncxx::document::value qry_doc = qry << builder::stream::finalize;

    std::cerr << bsoncxx::to_json(qry_doc.view()) << "\n";
    
    auto res = coll.insert_one(qry_doc.view());
    if (res)
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "Inserted obj " << res->result().inserted_count() << " "
		  << res->inserted_id().get_oid().value.to_string() << "\n";
	meta = lookup_object_meta(to);
    }
    else
    {
	BOOST_LOG_SEV(lg_, wslog::error) << "failed to insert workspace doc: " << bsoncxx::to_json(qry_doc.view());
    }

    return meta;
}

void WorkspaceDBQuery::set_object_size(const std::string &object_id, size_t size)
{
    std::cerr << "set " << object_id << " to size " << size << "\n";
    auto coll = object_collection();
    builder::stream::document filter, set;

    filter << "uuid" << object_id;
    set << "$set"
	<< builder::stream::open_document << "size" << static_cast<long>(size) << builder::stream::close_document;
    std::cerr << bsoncxx::to_json(filter.view())
	      << bsoncxx::to_json(set.view());

    bsoncxx::stdx::optional<mongocxx::result::update> result = coll.update_one(filter.view(), set.view());
}

bool WorkspaceDBQuery::remove_workspace_object(const ObjectMeta &meta, RemovalRequest &remreq)
{
    auto coll = object_collection();
    builder::stream::document filter;

    const WSPath &path = meta.ws_path;
    const std::string &obj_id = meta.id;

    filter << "uuid" << obj_id
	   << "name" << path.name
	   << "path" << path.path
	   << "workspace_uuid" << path.workspace.uuid;
    std::cerr << bsoncxx::to_json(filter.view()) << "\n";

    bsoncxx::stdx::optional<mongocxx::result::delete_result> result = coll.delete_one(filter.view());

    if (result)
    {
	auto &cfg = db_.config();
	
	std::cerr << "delete success " << result->deleted_count() << "\n";

	// Determine the files that need to be removed.

	if (meta.is_folder())
	{
	    remreq.add_directory(cfg.filesystem_path_for_object(path));
	}
	else if (meta.is_object())
	{
	    if (meta.shockurl.empty())
	    {
		remreq.add_file(cfg.filesystem_path_for_object(path));
	    }
	    else
	    {
		remreq.add_shock_object(meta.shockurl, meta.id);
	    }
	}

	return true;
    }
    else
    {
        BOOST_LOG_SEV(lg_, wslog::error) << "failed to remove WS item " << bsoncxx::to_json(filter.view()) << "\n";
	return false;
    }
}

/**
 * Remove a folder.
 *
 *   - Verify there are no objects in this folder.
 *   - Remove the folder object and
 *   - update removal request
 * 
 */
bool WorkspaceDBQuery::remove_workspace_folder_only(const ObjectMeta &meta, RemovalRequest &remreq)
{
    auto coll = object_collection();
    builder::stream::document filter;

    const WSPath &path = meta.ws_path;
    const std::string &obj_id = meta.id;

    filter << "uuid" << obj_id
	   << "name" << path.name
	   << "path" << path.path
	   << "workspace_uuid" << path.workspace.uuid;
    std::cerr << bsoncxx::to_json(filter.view()) << "\n";

    bsoncxx::stdx::optional<mongocxx::result::delete_result> result = coll.delete_one(filter.view());

    if (result)
    {
	auto &cfg = db_.config();
	
	std::cerr << "delete success " << result->deleted_count() << "\n";

	// Determine the files that need to be removed.

	if (meta.is_folder())
	{
	    remreq.add_directory(cfg.filesystem_path_for_object(path));
	}
	else if (meta.is_object())
	{
	    if (meta.shockurl.empty())
	    {
		remreq.add_file(cfg.filesystem_path_for_object(path));
	    }
	    else
	    {
		remreq.add_shock_object(meta.shockurl, meta.id);
	    }
	}

	return true;
    }
    else
    {
        BOOST_LOG_SEV(lg_, wslog::error) << "failed to remove WS item " << bsoncxx::to_json(filter.view()) << "\n";
	return false;
    }
}

/**
 * Remove a folder and its contents
 *
 *   - Enumerate all files.
 *   - Sort by number of slashes, largest first. This gives us an effective
 *     depth-first search.
 *   - Update removal request with file data at each level.
 *   - Do a single mongo query to remove all of the objects.
 * 
 */
bool WorkspaceDBQuery::remove_workspace_folder_and_contents(const ObjectMeta &meta, RemovalRequest &remreq)
{
    auto coll = object_collection();

    const WSPath &path = meta.ws_path;
    const std::string &obj_id = meta.id;

    // Query for contents of folder.
    
    builder::stream::document qry;

    qry << "path" 
	<< builder::stream::open_document << "$regex" << (path.full_path() + "($|/)") << builder::stream::close_document
	<< "workspace_uuid" << path.workspace.uuid;
    std::cerr << bsoncxx::to_json(qry.view()) << "\n";

    std::vector<ObjectMeta> objs;
    
    auto cursor = coll.find(qry.view());
    for (auto ent = cursor.begin(); ent != cursor.end(); ent++)
    {
	auto &obj = *ent;

	// object must be in the same workspace based on the query above
	ObjectMeta meta = metadata_from_db(path.workspace, obj);
	objs.emplace_back(meta);
    }
    for (auto &m: objs)
    {
	std::cerr << std::setw(20) << m.name << " " << m.path << "\n";
    }

    std::sort(objs.begin(), objs.end(), [](const ObjectMeta &a, const ObjectMeta &b) {
	return std::count(b.path.begin(), b.path.end(), '/') <
	    std::count(a.path.begin(), a.path.end(), '/');
	    });
    std::cerr << "after sort\n";
    for (auto &m: objs)
    {
	std::cerr << std::setw(40) << m.name << " " << m.path << "\n";
    }
    

    return false;
}

/*
 * Update this object in the database.
 *
 * It may be either a workspace or an object to be updated. We have
 * down ownership and correctness validations before coming here, so
 * our task is to look up the data, make modifications, and return
 * updated metadata.
 */
ObjectMeta WorkspaceDBQuery::update_object(const ObjectToModify &obj, bool append)
{
    ObjectMeta meta = lookup_object_meta(obj.parsed_path);

    std::cerr << "update: meta=" << meta << "\n";
    std::cerr << "  md=" << meta.user_metadata << "\n";
    std::cerr << "  type=" << meta.type << "\n";
    std::cerr << "  cre=" << meta.creation_time << "\n";

    if (obj.type && (is_folder(obj.type.value()) != is_folder(meta.type)))
    {
        BOOST_LOG_SEV(lg_, wslog::error) << "cannot change non-folder type to folder or vice versa";
	return {};
    }

    auto coll = (*client_)[db_.db_name()][obj.parsed_path.is_workspace_path() ? "workspaces" : "objects"];
    builder::stream::document filter;

    filter << "uuid" << meta.id;
    std::cerr << "Update filter " << bsoncxx::to_json(filter.view()) << "\n";

    builder::stream::document update;

    update << "$set"
	   << builder::stream::open_document;

    bool empty = true;
    if (obj.user_metadata)
    {
	empty = false;
	// If we are appending, initialize our map with current md. Otherwise not.
	std::map<std::string, std::string> to_update;
	if (append)
	    to_update = meta.user_metadata;
	to_update.insert(obj.user_metadata.value().begin(), obj.user_metadata.value().end());

	builder::stream::document  md;
	for (auto x: to_update)
	    md << x.first << x.second;
	update << "metadata" << md;
    }
    if (obj.type)
    {
	if (obj.type.value() == meta.type)
	{
	    std::cerr << "skipping update of type - value already set\n";
	}
	else
	{
	    empty = false;
	    update << "type" << obj.type.value();
	}
    }
    if (obj.creation_time)
    {
	empty = false;
	update << "creation_date" << obj.creation_time_str();
    }
    update << builder::stream::close_document;

    if (empty)
    {
	std::cerr << "Nothing to update\n";
	return meta;
    }
    std::cerr << "Update data " << bsoncxx::to_json(update.view()) << "\n";
    bsoncxx::stdx::optional<mongocxx::result::update> result = coll.update_one(filter.view(), update.view());
    if (result)
    {
	std::cerr << "updated\n";
	meta = lookup_object_meta(obj.parsed_path);
    }
    else
    {
	std::cerr << "update failed!\n";
    }
    return meta;
}

std::experimental::optional<std::string> WorkspaceDBQuery::update_permissions(const std::string &path_str,
									      const std::vector<UserPermission> &user_permissions,
									      const std::experimental::optional<WSPermission> &new_global_permission,
									      boost::json::array &output)
{
    WSPath path = parse_path(path_str);

    if (!path.is_workspace_path())
    {
	return "path is not a workspace path";
    }

    WSWorkspace &ws = path.workspace;

    if (ws.global_permission == WSPermission::public_)
    {
	if (token().user() != ws.owner && !admin_mode())
	{
	    return "modifications to published workspace must be performed by owner or administrator";
	}
    }
    else if (!user_has_permission(ws, WSPermission::admin))
    {
	return "permission denied";
    }

    for (auto up: user_permissions)
    {
	if (up.permission() == WSPermission::public_)
	    return "user permissions may not be set to publish";
    }

    /*
     * Create documents for the sets and unsets, and then construct
     * a single mongo request with the appropriate $set and $unset
     * fields requested.
     */

    builder::stream::document set_vals, unset_vals;

    if (new_global_permission)
    {
	WSPermission gp = *new_global_permission;
	if (gp == WSPermission::public_)
	{
	    if (token().user() != ws.owner)
		return "Only owner may set publish permission";
	}
	set_vals << "global_permission" << to_string(gp);
    }

    for (auto up: user_permissions)
    {
	if (up.permission() == WSPermission::none)
	{
	    unset_vals << ("permissions." + mongo_user_encode(up.user())) << "";
	}
	else
	{
	    set_vals << ("permissions." + mongo_user_encode(up.user())) << to_string(up.permission());
	}
    }

    if (set_vals.view().empty() && unset_vals.view().empty())
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "No values to set, returning\n";
	return {};
    }

    auto coll = workspace_collection();
    builder::stream::document filter;
    filter << "uuid" << ws.uuid;
    BOOST_LOG_SEV(lg_, wslog::debug) << "Update filter " << bsoncxx::to_json(filter.view()) << "\n";

    builder::stream::document update;
    
    if (!set_vals.view().empty())
    {
	update << "$set" << set_vals;
    }
    if (!unset_vals.view().empty())
    {
	update << "$unset" << unset_vals;
    }

    BOOST_LOG_SEV(lg_, wslog::debug) << "Update set " << bsoncxx::to_json(update.view()) << "\n";

    bsoncxx::stdx::optional<mongocxx::result::update> result = coll.update_one(filter.view(), update.view());
    if (result)
    {
	BOOST_LOG_SEV(lg_, wslog::debug)  << "updated\n";
    }
    else
    {
	BOOST_LOG_SEV(lg_, wslog::debug) << "update failed!\n";
    }

    return {};
}

/**
 * Copy semantics. (with help from the C++ standard library definitions
 * of the semantics of std::filesystem:copy)
 *
 *     - Determine status of from (look up metadata)
 *     - If from does not exist, error.
 *     - Determine status of to.
 *     - If from and to are the same file, error.
 *     - If from is a folder but to is a regular file, error.
 *     - If from is a folder file, then
 *        - If to does not exist, execute create_directory(to)
 *        - If recursive
 *           - iterate over each file f in from and copy(f, to/f.pathname)
 *     - Otherwise,
 *        - If to is a directory, then copy_file(from, to/from.filename, options)
 *        - Otherwise, copy_file(from, to, options)
 */

ObjectMeta WorkspaceDBQuery::perform_copy(const std::string &from, const std::string &to, bool recursive, bool overwrite)
{
    ObjectMeta m;

    WSPath from_path = parse_path(from);
    std::cerr << "parsed from " << from_path << "\n";
    if (from_path.workspace.uuid.empty())
    {
	m.error = "Error parsing from path";
	return m;
    }
    
    ObjectMeta from_meta = lookup_object_meta(from_path);

    if (!from_meta.valid)
    {
	m.error = "From object does not exist";
	return m;
    }

    const std::string &user = token().user();
    // Check permissions on source.
    if (!user_has_permission(from_path.workspace, WSPermission::read))
    {
	m.error = "Permission denied on from path";
	return m;
    }

    WSPath to_path = parse_path(to);
    if (to_path.workspace.uuid.empty())
    {
	m.error = "Error parsing to path";
	return m;
    }

    // Permission check on destination
    if (!user_has_permission(to_path.workspace, WSPermission::write))
    {
	m.error = "Permission denied on to path";
	return m;
    }
    
    ObjectMeta to_meta = lookup_object_meta(to_path);
    
    if (to_meta.valid)
    {
	if (from_meta.id == to_meta.id)
	{
	    m.error = "From and to are the same object";
	    return m;
	}

	if (from_meta.is_folder() && !to_meta.is_folder())
	{
	    m.error = "Cannot copy a folder onto an object";
	    return m;
	}
    }
    else
    {
	// If our destination does not exist, its containing
	// folder must exist.
	// Special case - check to see if the destination is a
	// workspace. We won't create a new workspace on demand here.
	WSPath parent_path = to_path.parent_path();
	ObjectMeta parent_meta = lookup_object_meta(parent_path);
	if (!parent_meta.valid)
	{
	    m.error = "Neither destination nor its parent exists";
	    return m;
	}
    }

    // Finished with initial error checking, start processing

    if (from_meta.is_folder())
    {
	if (!to_meta.valid)
	{
	    to_meta = create_folder(to_path);
	    std::cerr << "Created new path for dest " << to_meta << "\n";
	}
	if (recursive)
	{
	    // In the workspace, a copy is just making a copy of the
	    // database records with a different path. We can query the
	    // database for the entire hierarchy and replicate it.

	    std::vector<ObjectMeta> objs = list_objects(from_path, false, false, true);

	    // Sort by number of slashes so that we can emulate a breadth-first
	    // search so that containing folders are created before their contents
	    std::sort(objs.begin(), objs.end(), [](const ObjectMeta &a, const ObjectMeta &b) {
		return std::count(a.path.begin(), a.path.end(), '/') <
		    std::count(b.path.begin(), b.path.end(), '/');
	    });
	    
	    for (auto obj: objs)
	    {
		std::cerr << "would copy " << obj << "\n";
		auto dest_obj_path = obj.ws_path.replace_path_prefix(from_path, to_path);
		if (dest_obj_path)
		{
		    std::cerr << "new is " << *dest_obj_path << "\n\n";
		    copy_workspace_object(obj, *dest_obj_path);
		}
	    }
	}
    }
    else
    {
    }
    
    
    return m;
}

ObjectMeta WorkspaceDBQuery::perform_move(const std::string &from, const std::string &to, bool overwrite)
{
    ObjectMeta m;
    return m;
}

std::string WorkspaceDBQuery::generate_uuid()
{
    boost::uuids::uuid uuid = (*(db_.uuidgen()))();
    return boost::lexical_cast<std::string>(uuid);
}

ObjectMeta WorkspaceDBQuery::create_folder(const WSPath &path)
{
    ObjectToCreate cre(path, "folder");
    ObjectMeta created = create_workspace_object(cre, token().user());
    return created;
}
