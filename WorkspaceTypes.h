#ifndef _WorkspaceTypes_h
#define _WorkspaceTypes_h

#include <iostream>
#include <iomanip>
#include <ctime>
#include <map>
#include <vector>
#include <boost/regex.hpp>
#include <boost/json/array.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <experimental/optional>


inline std::string format_time(const std::tm &time)
{
    char tmstr[256];
    std::strftime(tmstr, sizeof(tmstr), "%Y-%m-%dT%H:%M:%SZ", &time);
    return tmstr;
}
			      
inline std::tm current_time()
{
    std::time_t t = std::time(nullptr);
    std::tm *tm = std::gmtime(&t);
    if (tm)
	return *tm;
    else
	throw std::runtime_error("error calling gmtime");
}

inline bool is_empty_time(const std::tm &time)
{
    return time.tm_year == 0 &&
	time.tm_mon == 0 &&
	time.tm_mday == 0 &&
	time.tm_hour == 0 &&
	time.tm_min == 0 &&
	time.tm_sec == 0;
}

enum class WSPermission : char
{
    invalid = 0,
	write = 'w',
	read = 'r',
	owner = 'o',
	admin = 'a',
	public_ = 'p',
	none = 'n',

};

inline std::string to_string(const WSPermission &p)
{
    char c[2];
    c[0] = static_cast<char>(p);
    c[1] = 0;
    return std::string(c);
}

inline WSPermission to_permission(char c)
{
    switch (std::tolower(c)) {
    case 'w':
	return WSPermission::write;
    case 'r':
	return WSPermission::read;
    case 'a':
	return WSPermission::admin;
    case 'n':
	return WSPermission::none;
    case 'o':
	return WSPermission::owner;
    case 'p':
	return WSPermission::public_;
    default:
	return WSPermission::invalid;
    }
}

inline WSPermission to_permission(const std::string &str)
{
    return to_permission(str.front());
}

inline WSPermission to_permission(char *s)
{
    return to_permission(s[0]);
}

inline std::ostream &operator<<(std::ostream &os, WSPermission p)
{
    switch (p)
    {
    case WSPermission::invalid:
	os << "invalid";
	break;
    case WSPermission::write:
	os << "write";
	break;
    case WSPermission::read:
	os << "read";
	break;
    case WSPermission::admin:
	os << "admin";
	break;
    case WSPermission::owner:
	os << "owner";
	break;
    case WSPermission::public_:
	os << "public";
	break;
    case WSPermission::none:
	os << "none";
	break;
    }
    return os;
}

class UserPermission
{
private:
    std::string user_;
    WSPermission permission_;

public:
    explicit UserPermission(const boost::json::value &p) {
	if (p.kind() == boost::json::kind::array)
	{
	    auto ar = p.as_array();
	    user_ = ar.at(0).as_string().c_str();
	    permission_ = to_permission(ar.at(1).as_string().c_str());
	    if (permission_ == WSPermission::invalid)
		throw std::runtime_error("UserPermission: invalid permission string");
		
	}
	else
	{
	    throw std::runtime_error("UserPermission: object was not an array");
	}
    }

    UserPermission(const UserPermission &p)
	: user_(p.user_)
	, permission_(p.permission_) {
    }

    UserPermission(UserPermission &&p)
	: user_(std::move(p.user_))
	, permission_(p.permission_) {
	std::cerr << "UP move construct\n";
    }

    const std::string & user() const { return user_;}
    WSPermission permission() const { return permission_; }
};


class WSWorkspace
{
public:
    std::string owner;
    std::string name;
    WSPermission global_permission;
    std::map<std::string, WSPermission> user_permission;
    std::map<std::string, std::string> metadata;
    std::string uuid;
    std::tm creation_time;

    explicit WSWorkspace()
	: global_permission(WSPermission::none)
	, creation_time({}) {}

    WSWorkspace(const WSWorkspace &w)
	: owner(w.owner)
	, name(w.name)
	, global_permission(w.global_permission)
	, user_permission(w.user_permission)
	, metadata(w.metadata)
	, uuid(w.uuid)
	, creation_time(w.creation_time) {
    }

    void serialize_permissions(boost::json::array &obj) {

	boost::json::array perm{"global_permission", to_string(global_permission) };
	obj.emplace_back(perm);
	for (auto elt: user_permission)
	{
	    obj.emplace_back(boost::json::array { elt.first, to_string(elt.second) });
	}
    }
    bool has_valid_name() const {
	return !name.empty() && name.find_first_of("/") == std::string::npos;
    }
};

class WSPath
{
public:
    WSWorkspace workspace;
    // the path in a WSPath is canonicalized; no leading /, no trailing /, no internal
    // multiple /. This is enforced by using the WSPathParser to create them from a string.
    std::string path;
    std::string name;
    bool empty;

    explicit WSPath(): empty(true) {}

    WSPath(const WSPath &p)
	: workspace(p.workspace)
	, path(p.path)
	, name(p.name)
	, empty(p.empty) {}
    
    // A workspace path is a path that does not have any user file component
    // (path and name are both empty)
    bool is_workspace_path() const { return path == "" && name == ""; }
    inline std::string full_path() const {
	std::string res = path;
	if (!res.empty())
	    res += "/";
	res += name;
	return res;
    }
    bool has_valid_name() const {
	return !name.empty() && name.find_first_of("/") == std::string::npos;
    }

    /**
     * Attempt to replace the from string at the start of the path with the to
     * string. If successful, returns an optional value containing the new WSPath.
     * If not successful, returns an empty value.
     */
    std::experimental::optional<WSPath> replace_path_prefix(const std::string &from, const std::string &to) {
	boost::regex match_re(from + "(/.*|)");

	boost::smatch sm;
	if (boost::regex_match(path, sm, match_re))
	{
	    WSPath ret = *this;
	    ret.path = to + sm[1];
	    return ret;
	}
	else
	{
	    return {};
	}
    }

    /**
     * Attempt to replace the from string at the start of the path with the to
     * string. If successful, returns an optional value containing the new WSPath.
     * If not successful, returns an empty value.
     *
     * If from and to have different workspaces, we replace the workspace as well.
     */
    std::experimental::optional<WSPath> replace_path_prefix(const WSPath &from, const WSPath &to) {

	// If from is a valid folder, it will have path=/some/path and name=dirname
	// So the path we wish to replace in our path is /some/path/dirname
	boost::regex match_re(from.full_path() + "(/.*|)");

	boost::smatch sm;
	if (boost::regex_match(path, sm, match_re))
	{
	    WSPath ret = *this;
	    ret.path = to.full_path() + sm[1];
	    if (ret.path == "/")
		ret.path = "";
	    ret.workspace = to.workspace;
	    return ret;
	}
	else
	{
	    std::cerr << "No match for " << match_re << " in " << path << "\n";
	    return {};
	}
    }

    std::vector<std::string> path_components() const {
	std::vector<std::string> ret;
	boost::algorithm::split(ret, path, boost::is_any_of("/"));
	return ret;
    }

    /**
     * Return the parent to this path.
     *
     * We move the last component of the path to the name field
     * and trim it from the path.
     *
     * If path is empty, we set the name to empty. (path of root is root)
     */
    WSPath parent_path() {
	WSPath ret = *this;
	if (path.empty())
	{
	    ret.name = "";
	    return ret;
	}
	
	int pos = path.rfind('/');

	if (pos == std::string::npos)
	{
	    ret.path = "";
	    ret.name = path;
	}
	else
	{
	    ret.path = path.substr(0, pos);
	    ret.name = path.substr(pos + 1);
	}
	return ret;
    }
	
};

class ObjectMeta
{
public:
    static struct ErrorMeta {} errorMeta;

    std::string name;
    std::string type;
    std::string path;
    std::tm creation_time;
    std::string id;
    std::string owner;
    size_t size;
    std::map<std::string, std::string> user_metadata;
    std::map<std::string, std::string> auto_metadata;
    WSPermission user_permission;
    WSPermission global_permission;
    std::string shockurl;
    bool valid;
    std::string error;
    WSPath ws_path;

    ObjectMeta(ErrorMeta, const std::string &err)
	: size(0)
	, creation_time({})
	, user_permission(WSPermission::none)
	, global_permission(WSPermission::none)
	, valid(false)
	, error(err)
	{}

    ObjectMeta()
	: size(0)
	, creation_time({})
	, user_permission(WSPermission::none)
	, global_permission(WSPermission::none)
	, valid(false)
	{}


    bool is_object() const {
	return valid && !name.empty() && !is_folder();
    }
    bool is_folder() const {
	return type == "folder" || type == "modelfolder";
    }
    boost::json::value serialize() {
	if (valid)
	{
	    boost::json::object am;
	    for (auto x: auto_metadata) {
		am.emplace(x.first, x.second);
	    }
	    am.emplace("is_folder", (type == "folder" || type == "modelfolder") ? 1 : 0);
										   
	    return boost::json::array({name, type, path,
		    creation_time, id, 
		    owner, size,
		    user_metadata, am, user_permission, global_permission, shockurl, error });
	}
	else
	{
	    return boost::json::array({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, error });

	}
    }
    std::string creation_time_str() const {
	return format_time(creation_time);
    }
};

/**
 * A struct wrapper for the object parameter passed to
 * the create method.
 *
 */
struct ObjectToCreate
{
    std::string path;
    WSPath parsed_path;
    std::string type;
    std::map<std::string, std::string> user_metadata;
    std::string object_data;
    std::tm creation_time;
    std::string shock_node;
    std::string uuid;

    /**
     * Construct from a WSPath and type.
     * Used in the creation of intermediate path components.
     */
    explicit ObjectToCreate(const WSPath &path_to_create, const std::string &type_str)
	: parsed_path(path_to_create)
	, type(type_str)
	, creation_time(current_time())
	{
	}


    /**
     * Construct from the input json.
     * Throw if the format is wrong.
     */
    explicit ObjectToCreate(const boost::json::value &val)
	: creation_time{ 0, 0, 0, 0, 0, 0 } {
	auto obj = val.as_array();
	path = obj[0].as_string().c_str();
	type = obj[1].as_string().c_str();
	if (obj.size() >= 3)
	{
	    auto m = obj[2].as_object();
	    for (auto elt: m)
	    {
		auto &val =  elt.value();
		switch (val.kind())
		{
		case boost::json::kind::double_:
		    user_metadata.emplace(std::make_pair(elt.key(), std::to_string(val.as_double())));
		    break;

		case boost::json::kind::int64:
		    user_metadata.emplace(std::make_pair(elt.key(), std::to_string(val.as_int64())));
		    break;

		case boost::json::kind::uint64:
		    user_metadata.emplace(std::make_pair(elt.key(), std::to_string(val.as_uint64())));
		    break;
		
		case boost::json::kind::string:
		    user_metadata.emplace(std::make_pair(elt.key(), val.as_string().c_str()));
		    break;
		
		default:
		    user_metadata.emplace(std::make_pair(elt.key(), ""));
		}
	    }
	}
	if (obj.size() >= 4 && obj[3].kind() == boost::json::kind::string)
	{
	    object_data = obj[3].as_string().c_str();
	}
	if (obj.size() >= 5 && obj[4].kind() == boost::json::kind::string)
	{
	    std::istringstream ss(obj[4].as_string().c_str());
	    ss >> std::get_time(&creation_time, "%Y-%m-%dT%H:%M:%SZ");
	}
    }

    std::string creation_time_str() const {
	return format_time(creation_time);
    }

    std::vector<std::string> path_components() const {
	std::vector<std::string> ret;
	boost::algorithm::split(ret, parsed_path.path, boost::is_any_of("/"));
	return ret;
    }
};

/**
 * A struct wrapper for the object parameter passed to
 * the update_metadata method.
 *
 */
struct ObjectToModify
{
    std::string path;
    WSPath parsed_path;
    std::experimental::optional<std::string> type;
    std::experimental::optional<std::map<std::string, std::string>> user_metadata;
    std::experimental::optional<std::tm> creation_time;

    /**
     * Construct from the input json.
     * Throw if the format is wrong.
     * typedef structure {
     *   list<tuple<FullObjectPath,UserMetadata,ObjectType,Timestamp creation_time>> objects;
     *   bool autometadata;
     *   bool append;
     *   bool adminmode;
     * } update_metadata_params
     */
    explicit ObjectToModify(const boost::json::value &val) {
	auto obj = val.as_array();
	path = obj[0].as_string().c_str();
	if (obj.size() >= 2 && obj[1].kind() == boost::json::kind::object)
	{
	    user_metadata.emplace(std::map<std::string, std::string>());
	    auto m = obj[1].as_object();
	    for (auto elt: m)
	    {
		auto &val =  elt.value();
		switch (val.kind())
		{
		case boost::json::kind::double_:
		    user_metadata->emplace(std::make_pair(elt.key(), std::to_string(val.as_double())));
		    break;

		case boost::json::kind::int64:
		    user_metadata->emplace(std::make_pair(elt.key(), std::to_string(val.as_int64())));
		    break;

		case boost::json::kind::uint64:
		    user_metadata->emplace(std::make_pair(elt.key(), std::to_string(val.as_uint64())));
		    break;
		
		case boost::json::kind::string:
		    user_metadata->emplace(std::make_pair(elt.key(), val.as_string().c_str()));
		    break;
		
		default:
		    user_metadata->emplace(std::make_pair(elt.key(), ""));
		}
	    }
	}
	if (obj.size() >= 3 && obj[2].kind() == boost::json::kind::string)
	{
	    type.emplace(obj[2].as_string().c_str());
	}
	if (obj.size() >= 4 && obj[3].kind() == boost::json::kind::string)
	{
	    std::istringstream ss(obj[3].as_string().c_str());
	    std::tm t;
	    ss >> std::get_time(&t, "%Y-%m-%dT%H:%M:%SZ");
	    creation_time.emplace(t);
	}
    }

    std::string creation_time_str() const {
	return format_time(*creation_time);
    }

    std::vector<std::string> path_components() const {
	return parsed_path.path_components();
    }
};

/**
 * A RemovalRequest maintains a set of objects to be removed from
 * either the filesystem or Shock.
 *
 * It is passed to the workspace database methods that remove the
 * database records for the objects and updated there.
 *
 * At a later point in a more appropriate thread (async Shock thread
 * or file I/O thread) the file or shock nodes will be removed.
 */
class RemovalRequest
{

private:

    /**
     * Shock object be removed. We keep the
     * workspace ID with the data in order to last-user-out removal
     * of the shock node.
     */
    struct ShockObj {
	std::string shock_url;
	std::string ws_id;
    };

    /**
     * List of shock objects to be removed.
     */
    std::vector<ShockObj> shock_urls_;

    /**
     * List of plain files to be removed.
     */
    std::vector<boost::filesystem::path> files_;

    /**
     * List of directories to be recursively removed
     */
    std::vector<boost::filesystem::path> directories_;

public:
    void add_shock_object(const std::string &url, const std::string &ws_id) {
	shock_urls_.emplace_back(ShockObj{url, ws_id});
    };

    void add_file(const boost::filesystem::path &p) {
	files_.emplace_back(p);
    }

    void add_directory(const boost::filesystem::path &p) {
	directories_.emplace_back(p);
    }

    friend inline std::ostream &operator<<(std::ostream &os, const RemovalRequest &r) {
	os << "RemovalRequest\n"
	   << "Shock:\n";
	for (auto &x: r.shock_urls_)
	{
	    os << "  " << x.shock_url << " " << x.ws_id << "\n";
	}
	os << "Files:";
	for (auto &x: r.files_)
	    os << " " << x;
	os << "Dirs:";
	for (auto &x: r.directories_)
	    os << " " << x;
	os << "\n";
	return os;
    }
};

inline std::ostream &operator<<(std::ostream &os, const std::map<std::string, std::string> &m)
{
    os << "{";
    auto iter = m.begin();
    if (iter != m.end())
    {
	os << "{" << iter->first << ":" << iter->second << "}";
	++iter;
    }
    for (; iter != m.end(); ++iter)
    {
	os << ",{" << iter->first << ":" << iter->second << "}";
    }
    os << "}";
    return os;
}


inline std::ostream &operator<<(std::ostream &os, const struct std::tm &t)
{
    os << "TM(" << t.tm_year
       << "," << t.tm_mon
       << "," << t.tm_mday
       << "," << t.tm_hour
       << "," << t.tm_min
       << "," << t.tm_sec
       << ")";
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const ObjectToCreate&c)
{
    os << "OTC(" << c.path
       << "," << c.type
       << "," << c.user_metadata
       << "," << c.object_data
       << "," << c.creation_time
       << ")";
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const UserPermission &up)
{
    os << "UP(" << up.user()
       << "," << up.permission()
       << ")";
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const ObjectToModify&c)
{
    os << "OTC(" << c.path
       << "," << (c.type ? c.type.value() : "<none>")
       << ",";
    if (c.user_metadata)
	os << c.user_metadata.value();
    else
	os << "<none>";
	
    os
       << "," << (c.creation_time ? format_time(c.creation_time.value()) : "<none>")
       << ")";
    return os;
}

namespace boost {
    namespace json {

	template<class T>
	struct to_value_traits< std::map<T, T > >
	{
	    static void assign( value& jv, std::map< T, T > const& t );
	};

	template< class T >
	inline void
	to_value_traits< std::map<T, T> >::
	assign( value& jv, std::map<T, T > const& t )
	{
	    object &o = jv.emplace_object();
	    for (auto x: t)
	    {
		o.emplace(x.first, x.second);
	    }
	}


	template <>
	struct to_value_traits< std::tm> 
	{
	    static void assign( value& jv, std::tm const& t ) {
		std::ostringstream os;
		os << std::put_time(&t, "%Y-%m-%dT%H:%M:%SZ");
		jv = os.str();
	    }
	};

	template <>
	struct to_value_traits< WSPermission > 
	{
	    static void assign( value& jv, WSPermission const& t ) {
		char c[2];
		c[0] = static_cast<char>(t);
		c[1] = '\0';
		jv = c;
	    }
	};



    }
}
inline std::ostream &operator<<(std::ostream &os, const WSWorkspace &w)
{
    os << "WSWorkspace("
       << w.name
       << "," << w.owner
       << "," << w.uuid
       << "," << w.global_permission
       << ",{";
    auto iter = w.user_permission.begin();
    if (iter != w.user_permission.end())
    {
	os << iter->first
	   << ":" << iter->second;
	++iter;
    }
    while (iter != w.user_permission.end())
    {
	os << ","
	   << iter->first
	   << ":" << iter->second;
	iter++;
    }
    os << "}," << std::put_time(&w.creation_time, "%Y-%m-%dT%H:%M:%SZ")
       << ")";
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const WSPath &p)
{
    os << "WSPath("
       << p.workspace
       << "," << p.path
       << "," << p.name
       << ")";
    return os;
}

inline std::ostream &operator<<(std::ostream &os, const ObjectMeta &m)
{
    os << "ObjectMeta("
       << m.name
       << "," << m.type
       << "," << m.path
       << "," << m.id
       << "," << m.owner
       << "," << m.shockurl
       << "," << (m.valid ? "VALID" : "NOT VALID" )
       << "," << m.error
       << ")";
    return  os;
}


inline bool is_folder(const std::string &type)
{
    return type == "folder" || type == "modelfolder";
}

#endif
