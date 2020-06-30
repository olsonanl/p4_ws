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
#include "WorkspaceTypes.h"
#include "Logging.h"

#include "JSONRPC.h"

class WorkspaceDBQuery;

class WorkspaceService
    : public wslog::LoggerBase
    , public std::enable_shared_from_this<WorkspaceService>
{
public:
private:


    std::shared_ptr<WorkspaceState> global_state_;

    typedef void (WorkspaceService::* ptr_to_method)(const JsonRpcRequest &req,
						     JsonRpcResponse &resp,
						     DispatchContext &dc,
						     int &http_code);

    enum class Authentication
    {
	none,
	optional,
	required
    };
    
    struct Method
    {
	ptr_to_method method;
	Authentication auth;
    };

    std::map<const boost::json::string, Method> method_map_;

public:
    WorkspaceService(std::shared_ptr<WorkspaceState> global_state)
	: global_state_(global_state)
	, wslog::LoggerBase("wssvc") {
	init_dispatch();
    }

    void dispatch(const JsonRpcRequest &req, JsonRpcResponse &resp,
		  DispatchContext &dc, int &http_code) {
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
		valid = global_state_->validate_certificate(dc.token);
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

    inline const WorkspaceConfig &config() const { return global_state_->config(); }
    
    
private:
    void init_dispatch() {
	method_map_.emplace(std::make_pair("ls",  Method { &WorkspaceService::method_ls, Authentication::optional }));
	method_map_.emplace(std::make_pair("get", Method { &WorkspaceService::method_get, Authentication::optional }));
	method_map_.emplace(std::make_pair("list_permissions", Method { &WorkspaceService::method_list_permissions, Authentication::optional }));
	method_map_.emplace(std::make_pair("get_download_url", Method { &WorkspaceService::method_get_download_url, Authentication::optional }));
    }

    void method_get(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_ls(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_list_permissions(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_get_download_url(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);

    void process_ls(std::unique_ptr<WorkspaceDBQuery> qobj,
		    DispatchContext &dc, boost::json::array &paths, boost::json::object &output,
		    bool excludeDirectories, bool excludeObjects, bool recursive, bool fullHierachicalOutput);
	
};

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
#endif
