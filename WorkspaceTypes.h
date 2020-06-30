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
	return valid && !name.empty() && type != "folder" && type != "modelfolder";
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


#endif
