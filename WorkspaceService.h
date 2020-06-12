#ifndef _WorkspaceService_h
#define _WorkspaceService_h

#include <ctime>
#include <memory>
#include <iostream>
#include <map>
#include <boost/asio/spawn.hpp>
#include <boost/json/traits.hpp>
#include "WorkspaceErrors.h"
#include "WorkspaceState.h"
#include "DispatchContext.h"

#include "JSONRPC.h"

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
		os << std::put_time(&t, "%Y-%m-%d:%H:%M:%SZ");
		jv = os.str();
		std::cerr << "seralized tm to " << os.str() << "\n";
	    }
	};



    }
}


class WSWorkspace
{
public:
    std::string owner;
    std::string name;
    std::string global_permission;
    std::map<std::string, std::string> user_permission;
    std::map<std::string, std::string> metadata;
    std::string uuid;
    std::tm creation_time;

    WSWorkspace()
	: creation_time({}) {}
};

class WSPath
{
public:
    WSWorkspace workspace;
    std::string path;
    std::string name;
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
    std::string user_permission;
    std::string global_permission;
    std::string shockurl;

    ObjectMeta()
	: size(0)
	, creation_time({}) 
	{}

    boost::json::value serialize() {
	return boost::json::array({name, type, path,
		    creation_time, id, 
		    owner, size,
		    user_metadata, auto_metadata, user_permission, global_permission, shockurl });
    }
};

class WorkspaceService
    : public std::enable_shared_from_this<WorkspaceService>
{
public:
private:

    std::shared_ptr<WorkspaceState> global_state_;

    typedef void (WorkspaceService::* ptr_to_method)(const JsonRpcRequest &req,
						     JsonRpcResponse &resp,
						     DispatchContext &dc,
						     boost::system::error_code &ec);
    std::map<const boost::json::string, ptr_to_method> method_map_;

public:
    WorkspaceService(std::shared_ptr<WorkspaceState> global_state)
	: global_state_(global_state) {
	init_dispatch();
    }

    void dispatch(const JsonRpcRequest &req, JsonRpcResponse &resp,
		  DispatchContext &dc, boost::system::error_code &ec) {
	std::cerr << "ws dispatching " << req << "\n";
	auto x = method_map_.find(req.method());
	if (x == method_map_.end())
	{
	    ec = WorkspaceErrc::MethodNotFound;
	    std::cerr << "method not found " << ec << "\n";
	    return;
	}
	ptr_to_method fp = x->second;
	(this->*fp)(req, resp, dc, ec);
    }


private:
    void init_dispatch() {
	method_map_.emplace(std::make_pair("ls", &WorkspaceService::method_ls));
	method_map_.emplace(std::make_pair("get", &WorkspaceService::method_get));
    }

    void method_get(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, boost::system::error_code	&);
    void method_ls(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, boost::system::error_code &);

};

inline std::ostream &operator<<(std::ostream &os, const WSWorkspace &w)
{
    os << "WSWorkspace("
       << w.name
       << "," << w.owner
       << "," << w.uuid
       << "," << std::put_time(&w.creation_time, "%Y-%m-%d:%H:%M:%SZ")
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


#endif
