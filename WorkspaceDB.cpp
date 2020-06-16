#include "WorkspaceDB.h"

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

#include <bsoncxx/builder/basic/array.hpp>
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
static bool url_decode(const std::string& in, std::string& out)
{
    out.clear();
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
	if (in[i] == '%')
	{
	    if (i + 3 <= in.size())
	    {
		int value = 0;
		std::istringstream is(in.substr(i + 1, 2));
		if (is >> std::hex >> value)
		{
		    out += static_cast<char>(value);
		    i += 2;
		}
		else
		{
		    return false;
		}
	    }
	    else
	    {
		return false;
	    }
	}
	else if (in[i] == '+')
	{
	    out += ' ';
	}
	else
	{
	    out += in[i];
	}
    }
    return true;
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
    auto val = doc[key];
    std::string v;
    if (val.type() == bsoncxx::type::k_utf8)
    {
	v = static_cast<std::string>(val.get_utf8().value);
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

WorkspaceDB::WorkspaceDB(const std::string &uri, int threads, const std::string &db_name)
    : db_name_(db_name)
    , uri_(uri)
    , pool_(uri_)
    , thread_pool_(threads) 
{
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
   
}

//void WorkspaceDB::list_workspaces(std::shared_ptr<std::vector<

/*
 * Query needs to post into the thread pool,
 * passing the yield context. 
 */

std::unique_ptr<WorkspaceDBQuery> WorkspaceDB::make_query(const AuthToken &token)
{
    auto pe = pool_.acquire();
    return std::make_unique<WorkspaceDBQuery>(token, std::move(pe), shared_from_this());
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
    }
    else
    {
	auto coll = (*client_)[db_->db_name()]["objects"];;
	auto qry = builder::basic::document{};

	qry.append(kvp("path", o.path));
	qry.append(kvp("name", o.name));
	qry.append(kvp("workspace_uuid", o.workspace.uuid));

	std::cerr << "qry: " << bsoncxx::to_json(qry.view()) << "\n";
	auto cursor = coll.find(qry.view());

	auto ent = cursor.begin();
	if (ent == cursor.end())
	{
	    std::cerr << "Not found\n";
	    return meta;
	}
	auto &obj = *ent;

	meta.name = get_string(obj, "name");
	meta.type = get_string(obj, "type");
	meta.path = "/" + o.workspace.owner + "/" + get_string(obj, "path");
	meta.creation_time = get_tm(obj, "creation_date");
	meta.id = get_string(obj, "uuid");
	meta.owner = get_string(obj, "owner");
	meta.size = get_int64(obj, "size");
	meta.user_metadata = get_map(obj, "metadata");
	meta.auto_metadata = get_map(obj, "autometadata");
	meta.user_permission = effective_permission(o.workspace);
	meta.global_permission = o.workspace.global_permission;
	if (get_int64(obj, "shock"))
	{
	    meta.shockurl = get_string(obj, "shocknode");
	}
    }

    return meta;
}

WSPath WorkspaceDBQuery::parse_path(const boost::json::string &pstr)
{
    std::cerr << "validating " << pstr << "\n";

    auto coll = (*client_)[db_->db_name()]["workspaces"];

    auto qry = builder::basic::document{};

    static const boost::regex username_path("/([^/]+)/([^/]+)(/(.*))?");
    static const boost::regex uuid_path("[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}");

    WSPath path;
    
    boost::cmatch res;
    if (boost::regex_match(pstr.begin(), pstr.end(), res, username_path))
    {
	path.workspace.owner = std::move(res[1].str());
	path.workspace.name = std::move(res[2].str());

	std::string file_path = std::move(res[4].str());

	if (!file_path.empty() && file_path[file_path.size() - 1] == '/')
	{
	    file_path.pop_back();
	}

	// If path is empty, we had either /user/ws or /user/ws/ which are
	// equivalent here and require no further parsing.
	if (!file_path.empty())
	{
	    std::vector<std::string> comps;
	    boost::algorithm::split(comps, file_path, boost::is_any_of("/"));
	    path.name = std::move(comps.back());
	    comps.pop_back();
	    path.path = boost::algorithm::join(comps, "/");
	}

	qry.append(kvp("owner", path.workspace.owner));
	qry.append(kvp("name", path.workspace.name));
    
 	// std::cerr << "qry: " << bsoncxx::to_json(qry.view()) << "\n";

	auto cursor = coll.find(qry.view());
	auto ent = cursor.begin();

	if (ent == cursor.end())
	    return WSPath();

	auto &obj = *ent;

	std::cerr << "obj: " << bsoncxx::to_json(obj) << "\n";

	auto uuid = obj["uuid"];
	auto cdate = obj["creation_date"];
	auto perm = obj["global_permission"];
	auto all_perms = get_perm_map(obj, "permissions");

	ent++;
	if (ent != cursor.end())
	{
	    std::cerr << "nonunique workspace!\n";
	}

	path.workspace.uuid = static_cast<std::string>(uuid.get_utf8().value);
	path.workspace.global_permission = to_permission(perm.get_utf8().value[0]);

	/* parse time. */

	std::string creation_time(static_cast<std::string>(cdate.get_utf8().value));
	std::istringstream ss(creation_time);
	ss >> std::get_time(&path.workspace.creation_time, "%Y-%m-%dT%H:%M:%SZ");
	if (ss.fail())
	{
	    std::cerr << "creation date parse failed '" << creation_time << "'\n";
	}

	for (auto p: all_perms)
	{
	    // Key here is the user id, url-encoded.
	    std::string user;
	    if (url_decode(p.first, user))
	    {
		path.workspace.user_permission.insert(std::make_pair(user, p.second));
		// std::cerr << user << ": " << p.second << "\n";
	    }
	}
    }
    else
    {
	std::cerr << "No match for " << pstr<< "\n";
    }
    
    
    return path;
}

WSPermission WorkspaceDBQuery::effective_permission(const WSWorkspace &w)
{
    std::cerr << "compute permission for user " << token().user() << "and workspace owned by " << w.owner << "\n";
    if (w.global_permission == WSPermission::public_)
    {
	std::cerr << "  is public\n";
	return WSPermission::public_;
    }

    if (w.owner == token().user())
    {
	std::cerr << "  is owner\n";
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

    auto has_perm = w.user_permission.find(token().user());
    if (has_perm != w.user_permission.end())
    {
	WSPermission user_perm = has_perm->second;
	int rank_user = perm_ranks[user_perm];
	int rank_global = perm_ranks[w.global_permission];

	if (rank_user > rank_global)
	{
	    std::cerr << "  ranks " << rank_user << " " << rank_global << " determined " << user_perm << "\n";
	    return user_perm;
	}
    }
    std::cerr << "  global fallback\n";
    return w.global_permission;
}
