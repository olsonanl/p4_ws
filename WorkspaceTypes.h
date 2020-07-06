#ifndef _WorkspaceTypes_h
#define _WorkspaceTypes_h

#include <iostream>
#include <boost/json/array.hpp>

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

    WSWorkspace()
	: global_permission(WSPermission::none)
	, creation_time({}) {}

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

    WSPath(): empty(true) {}
    
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
};

class ObjectMeta
{
public:
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
		    user_metadata, am, user_permission, global_permission, shockurl });
	}
	else
	{
	    return boost::json::array({});
	}
    }
};

/**
 * A struct wrapper for the object parameter passed to
 * the create method.
 */
struct ObjectToCreate
{
    std::string path;
    WSPath parsed_path;
    std::string type;
    std::map<std::string, std::string> user_metadata;
    std::string object_data;
    std::tm creation_time;

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
};

inline std::ostream &operator<<(std::ostream &os, std::map<std::string, std::string> m)
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


inline std::ostream &operator<<(std::ostream &os, struct std::tm &t)
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

inline std::ostream &operator<<(std::ostream &os, ObjectToCreate&c)
{
    os << "OTC(" << c.path
       << "," << c.type
       << "," << c.user_metadata
       << "," << c.object_data
       << "," << c.creation_time
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
       << ")";
    return  os;
}

inline bool is_folder(const std::string &type)
{
    return type == "folder" || type == "modelfolder";
}

#endif
