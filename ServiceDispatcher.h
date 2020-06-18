#ifndef _ServiceDispatcher_h
#define _ServiceDispatcher_h

/*
 * Given a JSONRPC request, dispatch to the appropriate service object.
 */

#include <memory>
#include <map>
#include <string>
#include <boost/system/error_code.hpp>
#include "JSONRPC.h"
#include "WorkspaceErrors.h"

class ServiceDispatcher
    : public std::enable_shared_from_this<ServiceDispatcher>
{
    using dispatch_cb = std::function<void(const JsonRpcRequest &,
					   JsonRpcResponse &,
					   DispatchContext &dc,
					   int &http_code)>;
    using dispatch_map = std::map<boost::json::string, dispatch_cb>;
    dispatch_map map_;

public:
    ServiceDispatcher () { }
    ~ServiceDispatcher() {
	map_.clear();
    }

    void register_service(const boost::json::string &name, dispatch_cb cb) {
	map_[name] = cb;
    }

    void unregister_service(const boost::json::string &name) {
	auto x = map_.find(name);
	if (x != map_.end())
	{
	    map_.erase(x);
	}
    }

    void dispatch(const JsonRpcRequest &req, JsonRpcResponse &resp,
		  DispatchContext &dc, int &http_code) {
	auto x = map_.find(req.service());
	if (x == map_.end())
	{
	    http_code = 500;
	    resp.set_error(-32601, "Service not found");
	    return;
	}
	x->second(req, resp, dc, http_code);
    }

};

#endif
