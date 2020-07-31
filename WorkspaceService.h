#ifndef _WorkspaceService_h
#define _WorkspaceService_h

#include <ctime>
#include <memory>
#include <iostream>
#include <map>
#include <set>
#include <boost/chrono/include.hpp>

#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl.hpp>

#include <boost/json/traits.hpp>
#include "WorkspaceErrors.h"
#include "DispatchContext.h"
#include "WorkspaceTypes.h"
#include "Logging.h"
#include "PendingUpload.h"
#include "Shock.h"

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
    boost::asio::io_context &ioc_;
    
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

    boost::asio::ssl::context &ssl_ctx_;

    /**
     * The service maintains an I/O context to host a single thread
     * used to serialize access to Shock and to the Shock
     * bookkeeping routines.
     */
    boost::asio::io_context shock_ioc_;

    /**
     * The Shock thread.
     */
    std::thread shock_thread_;

    /**
     * Set of pending Shock uploads. Entries are added here when
     * objects are created with createUploadNodes is specified.
     * The check_shock_ timer is used to periodically check
     * the Shock server to see if a file has been added.
     */
    std::map<std::string, PendingUpload> pending_uploads_;

    /**
     * Asio timer used to monitor the Shock pending uploads.
     */
    boost::asio::deadline_timer timer_;

    Shock shock_;

public:
    WorkspaceService(boost::asio::io_context &ioc, boost::asio::ssl::context &ssl_ctx,
		     WorkspaceDB &db, WorkspaceState &state);
    ~WorkspaceService() {
	BOOST_LOG_SEV(lg_, wslog::debug) << "destroy WorkspaceService";
	timer_.cancel_one();
	shock_ioc_.stop();
	
	BOOST_LOG_SEV(lg_, wslog::debug) << "join shock thread";
	shock_thread_.join();
	BOOST_LOG_SEV(lg_, wslog::debug) << "WorkspaceService done";
    }
    

    void dispatch(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    WorkspaceState &shared_state() { return shared_state_; }
    WorkspaceDB &db() { return db_; }
    
private:

    void run_timer(boost::asio::yield_context yield);

    /**
     * Check this pending upload to see if it has been completed.
     * Updates the size and valid fields on the upload if it has.
     */
    void check_pending_upload(PendingUpload &p, boost::asio::yield_context yield);

    void init_dispatch() {
	method_map_.emplace(std::make_pair("create",  Method { &WorkspaceService::method_create, Authentication::required }));
	method_map_.emplace(std::make_pair("delete",  Method { &WorkspaceService::method_delete, Authentication::required }));
	method_map_.emplace(std::make_pair("copy",  Method { &WorkspaceService::method_copy, Authentication::required }));
	method_map_.emplace(std::make_pair("ls",  Method { &WorkspaceService::method_ls, Authentication::optional }));
	method_map_.emplace(std::make_pair("get", Method { &WorkspaceService::method_get, Authentication::optional }));
	method_map_.emplace(std::make_pair("list_permissions", Method { &WorkspaceService::method_list_permissions, Authentication::optional }));
	method_map_.emplace(std::make_pair("set_permissions", Method { &WorkspaceService::method_set_permissions, Authentication::required }));
	method_map_.emplace(std::make_pair("get_download_url", Method { &WorkspaceService::method_get_download_url, Authentication::optional }));
	method_map_.emplace(std::make_pair("update_auto_meta", Method { &WorkspaceService::method_update_auto_meta, Authentication::optional }));
	method_map_.emplace(std::make_pair("update_metadata", Method { &WorkspaceService::method_update_metadata, Authentication::required }));
	method_map_.emplace(std::make_pair("update_metadata", Method { &WorkspaceService::method_update_metadata, Authentication::required }));
    }

    void method_copy(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_create(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_delete(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_get(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_ls(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_list_permissions(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_set_permissions(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_get_download_url(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_update_auto_meta(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);
    void method_update_metadata(const JsonRpcRequest &req, JsonRpcResponse &resp, DispatchContext &dc, int &http_code);

    void process_create(WorkspaceDBQuery & qobj, DispatchContext &dc,
			ObjectToCreate &to_create, boost::json::value &ret_value,
			const std::string &permission, bool createUploadNodes, bool downloadFromLinks, bool overwrite,
			const std::string &setowner, RemovalRequest &remreq);
    void process_ls(std::unique_ptr<WorkspaceDBQuery> qobj,
		    DispatchContext &dc, boost::json::array &paths, boost::json::object &output,
		    bool excludeDirectories, bool excludeObjects, bool recursive, bool fullHierachicalOutput);
	
};

#endif
