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
    switch (elt.type())
    {
    case bsoncxx::type::k_utf8:
	return static_cast<std::string>(elt.get_utf8().value);

    case bsoncxx::type::k_int64:
	long ival = elt.get_int64().value;
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

    auto val = doc[key];

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

    auto val = doc[key];

    if (!val)
	return map;

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

static long get_int64(const bsoncxx::document::view &doc, const std::string &key)
{
    long v = {};

    auto val = doc[key];
    if (val.type() == bsoncxx::type::k_int64)
    {
	v = static_cast<long>(val.get_int64().value);
    }
    return v;
}

static std::tm get_tm(const bsoncxx::document::view &doc, const std::string &key)
{
    std::tm v = {};

    auto val = doc[key];
    if (val.type() == bsoncxx::type::k_utf8)
    {
	std::string str = static_cast<std::string>(val.get_utf8().value);
	std::istringstream ss(str);
	ss >> std::get_time(&v, "%Y-%m-%dT%H:%M:%SZ");
    }
    return v;
}

#if 0
WorkspaceDB::WorkspaceDB(const std::string &uri, int threads, const std::string &db_name)
    : db_name_(db_name)
    , uri_(uri)
    , pool_(uri_)
    , thread_pool_(threads)
    , wslog::LoggerBase("wsdb")
{
#if 0
    /* howto. */

    mongocxx::pool::entry c = pool_.acquire();
    mongocxx::database db = (*c)["WorkspaceBuild"];
    mongocxx::collection coll = db["workspaces"];

    std::vector<bsoncxx::document::value> v;

    {
	mongocxx::cursor cursor = coll.find({});
	for (bsoncxx::document::view doc : cursor) {
	    //bsoncxx::document::value sdoc(doc);
	    v.emplace_back(doc);
	    //std::cout << bsoncxx::to_json(doc) << "\n";
	}
    }
    for (auto i = 0; i < std::min(5LU, v.size()); i++)
	std::cout << bsoncxx::to_json(v[i]) << "\n";
#endif   
}
#endif

bool WorkspaceDB::init_database(const std::string &uri, int threads, const std::string &db_name)
{
    db_name_ = db_name;
    uri_ = static_cast<mongocxx::uri>(uri);
    pool_ = std::make_unique<mongocxx::pool>(uri_);
    n_threads_ = threads;
    thread_pool_ = std::make_unique<boost::asio::thread_pool>(threads);

    // Start up our sync thread
    sync_thread_ = std::thread([this]() {
	std::cerr << "starting sync thread " << std::this_thread::get_id() << "\n";
	net::executor_work_guard<net::io_context::executor_type> guard
	    = net::make_work_guard(sync_ioc_);
	sync_ioc_.run(); 

	std::cerr << "exiting sync thread " << std::this_thread::get_id() << "\n";
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
	auto coll = (*client_)[db_.db_name()]["objects"];;
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
    meta.path = "/" + ws.owner + "/" + ws.name + "/" + get_string(obj, "path");
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

    BOOST_LOG_SEV(lg_, wslog::debug) << "populate "<< ws << "from obj " << bsoncxx::to_json(obj);

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
    auto coll = (*client_)[db_.db_name()]["workspaces"];
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

    BOOST_LOG_SEV(lg_, wslog::debug) << "obj: " << bsoncxx::to_json(obj) << "\n";

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
    auto coll = (*client_)[db_.db_name()]["objects"];

    auto qry = builder::basic::document{};

    qry.append(kvp("workspace_uuid", path.workspace.uuid));
    if (excludeDirectories)
	qry.append(kvp("folder", 0));
    else if (excludeObjects)
	qry.append(kvp("folder", 1));

    if (recursive)
    {
	std::string regex_str = "^" + path.full_path();
	std::cerr << "RECUR " << path << "'" << regex_str << "'\n";
        auto regex = bsoncxx::types::b_regex(regex_str);
	qry.append(kvp("path", regex));
    }
    else
    {
	qry.append(kvp("path", path.full_path()));
    }
    auto cursor = coll.find(qry.view());
    BOOST_LOG_SEV(lg_, wslog::debug) << "qry: " << bsoncxx::to_json(qry.view()) << "\n";

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
    auto coll = (*client_)[db_.db_name()]["workspaces"];

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
    
    BOOST_LOG_SEV(lg_, wslog::debug) << "qry: " << bsoncxx::to_json(qry.view()) << "\n";
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
    auto coll = (*client_)[db_.db_name()]["downloads"];

    std::time_t expires = time(0) + db_.config().download_lifetime();
    
    qry << "workspace_path" << path_str.c_str()
	<< "download_key" << key
	<< "expiration_time" << expires
	<< "name" << meta.name
	<< "size" << static_cast<std::int64_t>(meta.size);
    
    if (meta.shockurl.empty())
    {
	qry << "file_path" << db_.config().filesystem_path_for_object(path);
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
    BOOST_LOG_SEV(lg_, wslog::debug) << "QRY: " << bsoncxx::to_json(qry.view());

    return key;
}

bool WorkspaceDBQuery::lookup_download(const std::string &key, std::string &name, size_t &size,
				       std::string &shock_node, std::string &token,
				       std::string &file_path)
{
    auto coll = (*client_)[db_.db_name()]["downloads"];
    builder::stream::document qry;
    qry << "download_key" << key;
    auto res = coll.find_one(qry.view());
    if (res)
    {
	auto obj = res->view();
	std::cerr << "Found: " << bsoncxx::to_json(res->view()) << "\n";
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

ObjectMeta WorkspaceDBQuery::create_workspace_object(const ObjectToCreate &tc)
{
    ObjectMeta meta;
    std::cerr << "create " << tc << " " << tc.parsed_path << "\n";

    auto coll = (*client_)[db_.db_name()]["objects"];
    builder::stream::document qry;

    boost::uuids::uuid uuidobj = (*(db_.uuidgen()))();
    qry << "uuid" << boost::lexical_cast<std::string>(uuidobj);
    qry << "creation_date" << tc.creation_time_str();

    builder::stream::document user_metadata;
    for (auto x: tc.user_metadata)
	user_metadata << x.first << x.second;
    qry << "user_metadata" << user_metadata;
    qry << "folder" << (is_folder(tc.type) ? 1 : 0);
    qry << "name" << tc.parsed_path.name;
    qry << "path" << tc.parsed_path.path;
    // FIX OWNER qry << "owner" <<
    qry << "workspace_uuid" << tc.parsed_path.workspace.uuid;
    
    std::cerr << bsoncxx::to_json(qry.view()) << "\n";
/*
    auto res = coll.find_one(qry.view());
    if (res)
    {
	auto obj = res->view();
	std::cerr << "Found: " << bsoncxx::to_json(res->view()) << "\n";
	name = get_string(obj, "name");
	size = obj["size"].get_int64().value;
	shock_node = get_string(obj, "shock_node");
	token = get_string(obj, "token");
	file_path = get_string(obj, "file_path");
	return true;
    }
*/  

return meta;
}
