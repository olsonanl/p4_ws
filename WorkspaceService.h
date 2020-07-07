#ifndef _WorkspaceService_h
#define _WorkspaceService_h

#include <ctime>
#include <memory>
#include <iostream>
#include <map>
#include <boost/asio/spawn.hpp>
#include <boost/json/traits.hpp>
#include "WorkspaceErrors.h"
#include "DispatchContext.h"
#include "WorkspaceTypes.h"
#include "Logging.h"

#include "JSONRPC.h"

class WorkspaceDB;
class WorkspaceDBQuery;
class WorkspaceState;
class WorkspaceConfig;

class WorkspaceService
    : public wslog::LoggerBase
    , public std::enable_shared_from_this<WorkspaceService>
{
public:
private:
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

    WorkspaceDB &db_;
    WorkspaceState &shared_state_;

public:
    WorkspaceService(WorkspaceDB &db, WorkspaceState &state)
	: wslog::LoggerBase("wssvc")
	, db_(db)
	, shared_state_(state) {
	init_dispatch();
    }

    void dispatch(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    WorkspaceState &shared_state() { return shared_state_; }
    WorkspaceDB &db() { return db_; }
    
private:
    void init_dispatch() {
	method_map_.emplace(std::make_pair("create",  Method { &WorkspaceService::method_create, Authentication::required }));
	method_map_.emplace(std::make_pair("ls",  Method { &WorkspaceService::method_ls, Authentication::optional }));
	method_map_.emplace(std::make_pair("get", Method { &WorkspaceService::method_get, Authentication::optional }));
	method_map_.emplace(std::make_pair("list_permissions", Method { &WorkspaceService::method_list_permissions, Authentication::optional }));
	method_map_.emplace(std::make_pair("get_download_url", Method { &WorkspaceService::method_get_download_url, Authentication::optional }));
    }

    void method_create(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_get(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_ls(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_list_permissions(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_get_download_url(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);

    void process_create(WorkspaceDBQuery & qobj, DispatchContext &dc,
			ObjectToCreate &to_create, boost::json::value &ret_value,
			const std::string &permission, bool createUploadNodes, bool downloadFromLinks, bool overwrite,
			const std::string &setowner);
    void process_ls(std::unique_ptr<WorkspaceDBQuery> qobj,
		    DispatchContext &dc, boost::json::array &paths, boost::json::object &output,
		    bool excludeDirectories, bool excludeObjects, bool recursive, bool fullHierachicalOutput);
	
};

#endif
