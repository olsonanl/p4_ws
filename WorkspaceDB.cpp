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

std::unique_ptr<WorkspaceDBQuery> WorkspaceDB::make_query()
{
    auto pe = pool_.acquire();
    return std::make_unique<WorkspaceDBQuery>(std::move(pe), shared_from_this());
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
	// meta.user_permission = ;
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
    
	std::cerr << "qry: " << bsoncxx::to_json(qry.view()) << "\n";

	auto cursor = coll.find(qry.view());

	auto ent = cursor.begin();
	if (ent == cursor.end())
	    return WSPath();

	auto uuid = (*ent)["uuid"];
	auto cdate = (*ent)["creation_date"];

	ent++;
	if (ent != cursor.end())
	{
	    std::cerr << "nonunique workspace!\n";
	}
	std::cerr << "uuid=" << uuid.get_utf8().value << "\n";

	path.workspace.uuid = static_cast<std::string>(uuid.get_utf8().value);

	/* parse time. */

	std::string creation_time(static_cast<std::string>(cdate.get_utf8().value));
	std::istringstream ss(creation_time);
	ss >> std::get_time(&path.workspace.creation_time, "%Y-%m-%dT%H:%M:%SZ");
	if (ss.fail())
	{
	    std::cerr << "creation date parse failed '" << creation_time << "'\n";
	}
    }
    else
    {
	std::cerr << "No match for " << pstr<< "\n";
    }
    
    
    return path;
}
