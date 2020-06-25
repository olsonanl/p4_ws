#ifndef _Workspace_DB
#define _Workspace_DB

#include <memory>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/pool.hpp>

#include <boost/asio/coroutine.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/post.hpp>

#include "WorkspaceService.h"
#include "WorkspaceTypes.h"
#include "Logging.h"

class WorkspaceDB;

class WorkspaceDBQuery
    : public wslog::LoggerBase
{
private:
    const AuthToken &token_;
    bool admin_mode_;
    mongocxx::pool::entry  client_;
    std::shared_ptr<WorkspaceDB> db_;
public:
    WorkspaceDBQuery(const AuthToken &token, bool admin_mode, mongocxx::pool::entry p, std::shared_ptr<WorkspaceDB> db)
	: token_(token)
	, admin_mode_(admin_mode)
	, client_(std::move(p))
	, db_(db)
	, wslog::LoggerBase("wsdbq") {
    }
    ~WorkspaceDBQuery() { BOOST_LOG_SEV(lg_, wslog::debug) << "destroy WorkspaceDBQuery\n"; }
    WSPath parse_path(boost::json::string p);
    ObjectMeta lookup_object_meta(const WSPath &path);
    const AuthToken &token() { return token_; }
    bool admin_mode() const { return admin_mode_; }

    /*
     * Utility methods.
     */

    /*
     * Calculate effective permission for a workspace.
     */
    WSPermission effective_permission(const WSWorkspace &w);
    bool user_has_permission(const WSWorkspace &w, WSPermission min_permission);

    std::vector<ObjectMeta> list_objects(const WSPath &path, bool excludeDirectories, bool excludeObjects, bool recursive);
    std::vector<ObjectMeta> list_workspaces(const std::string &owner);

    void populate_workspace_from_db(WSWorkspace &ws);
    void populate_workspace_from_db_obj(WSWorkspace &ws, const bsoncxx::document::view &obj);

    ObjectMeta metadata_from_db(const WSWorkspace &ws,  const bsoncxx::document::view &obj);

};

class WorkspaceDB
    : public wslog::LoggerBase
    , public std::enable_shared_from_this<WorkspaceDB>
{
    std::string db_name_;
    mongocxx::uri uri_;
    mongocxx::pool pool_;
    mongocxx::instance instance_;

    boost::asio::thread_pool thread_pool_;

public:
    WorkspaceDB(const std::string &uri, int threads, const std::string &db_name);

    ~WorkspaceDB() { BOOST_LOG_SEV(lg_, wslog::debug) << "destroy WorkspaceDB\n"; }

    std::unique_ptr<WorkspaceDBQuery> make_query(const AuthToken &token, bool admin_mode = false);

    const std::string &db_name() { return db_name_; }
    boost::asio::thread_pool &thread_pool() { return thread_pool_; }

    template<typename Func>
    void run_in_thread(DispatchContext &dc, Func qfunc) {
	dc.timer.expires_at(boost::posix_time::pos_infin);
	boost::asio::post(thread_pool_,
			  [&dc, qfunc, this]() {
			      auto q = make_query(dc.token, dc.admin_mode);
			      qfunc(std::move(q));
			      dc.timer.cancel_one();
			  });
	
	boost::system::error_code ec({});
	dc.timer.async_wait(dc.yield[ec]);
	if (ec != boost::asio::error::operation_aborted)
	    BOOST_LOG_SEV(lg_, wslog::error) << "async_wait: " << ec.message() << "\n";

    }


};


#endif
