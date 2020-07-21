#ifndef _Workspace_DB
#define _Workspace_DB

#include <memory>
#include <thread>
#include <experimental/optional>

#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/pool.hpp>

#include <boost/asio/coroutine.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/post.hpp>
#include <boost/thread/tss.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "WorkspaceTypes.h"
#include "DispatchContext.h"
#include "Logging.h"

class WorkspaceDB;
class WorkspaceConfig;

class WorkspaceDBQuery
    : public wslog::LoggerBase
{
private:
    const AuthToken &token_;
    bool admin_mode_;
    mongocxx::pool::entry  client_;
    WorkspaceDB &db_;

public:
    WorkspaceDBQuery(const AuthToken &token, bool admin_mode, mongocxx::pool::entry p, WorkspaceDB &db)
	: token_(token)
	, admin_mode_(admin_mode)
	, client_(std::move(p))
	, db_(db)
	, wslog::LoggerBase("wsdbq") {
    }
    ~WorkspaceDBQuery() { BOOST_LOG_SEV(lg_, wslog::debug) << "destroy WorkspaceDBQuery\n"; }
    WSPath parse_path(const boost::json::value &p);
    WSPath parse_path(const boost::json::string &p);
    WSPath parse_path(const std::string &p);
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

    /*
     * Download support.
     */
    std::string insert_download_for_object(const boost::json::string &path_str, const AuthToken &ws_token,
					   ObjectMeta &meta, std::vector<std::string> &shock_urls);
    bool lookup_download(const std::string &key, std::string &name, size_t &size,
			 std::string &shock_node, std::string &token,
			 std::string &file_path);

    void set_object_size(const std::string &object_id, size_t size);
    
    /**
     * Create a workspace.
     * We assume here that we have already performed any requisite checks
     * for overwrites, permission, etc. This just formats and performs the database operation.
     */
    std::string create_workspace(const ObjectToCreate &tc);

    /**
     * Create a workspace object.
     * We assume here that we have already performed any requisite checks
     * for overwrites, permission, etc. This just formats and performs the database operation.
     */
    ObjectMeta create_workspace_object(const ObjectToCreate &tc, const std::string &owner);

    bool remove_workspace_object(const WSPath &path, const std::string &obj_id);

    /**
     * Update workspace or object metadata.
     */
    ObjectMeta update_object(const ObjectToModify &obj, bool append);

    /**
     * Update workspace permissions.
     */
    std::experimental::optional<std::string>  update_permissions(const std::string &path,
								 const std::vector<UserPermission> &user_permissions,
								 const std::experimental::optional<WSPermission> &new_global_permission,
								 boost::json::array &output);
};

class WorkspaceState;

class WorkspaceDB
    : public wslog::LoggerBase
{
    std::string db_name_;
    mongocxx::uri uri_;
    std::unique_ptr<mongocxx::pool> pool_;
    /**
     * The sync IOC is used to host a single thread
     * to which requests for database operations that must be
     * globally serialized are made
     */
    boost::asio::io_context sync_ioc_;
    /**
     * This is the thread running for the sync_ioc_;
     */
    std::thread sync_thread_;

    mongocxx::instance instance_;

    WorkspaceConfig &config_;
    int n_threads_;

    std::unique_ptr<boost::asio::thread_pool> thread_pool_;

    boost::thread_specific_ptr<boost::uuids::random_generator> uuidgen_;
public:
    WorkspaceDB(WorkspaceConfig &config)
	: wslog::LoggerBase("wsdb")
	, config_(config)
	, n_threads_(1) {
	
    }

    ~WorkspaceDB() {
	BOOST_LOG_SEV(lg_, wslog::debug) << "destroy WorkspaceDB";
	sync_ioc_.stop();
	
	BOOST_LOG_SEV(lg_, wslog::debug) << "join sync thread";
	sync_thread_.join();
	BOOST_LOG_SEV(lg_, wslog::debug) << "WorkspaceDB done";
    }

    bool init_database(const std::string &uri, int threads, const std::string &db_name);

    std::unique_ptr<WorkspaceDBQuery> make_query(const AuthToken &token, bool admin_mode = false);

    const std::string &db_name() { return db_name_; }
    WorkspaceConfig &config() { return config_; }

    /**
     * Execute the given function in a thread in the thread pool.
     * We use the timer in the DispatchContext to wake up the asynchronous
     * coroutine when the query completes; this is done by taking advantage
     * of the behavior that a canceled timer is an event that will trigger
     * the awakening of the coroutine.
     *
     * We also use the \ref make_query method to create a mongocxx pool
     * connection handle for the use of this thread.
     */
    template<typename Func>
    void run_in_thread(DispatchContext &dc, Func qfunc) {
	dc.timer.expires_at(boost::posix_time::pos_infin);
	boost::asio::post(*(thread_pool_.get()),
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
    /**
     * Same as  \ref run_in_thread, except we run in the single sync
     * thread. Used for serializing metadata operations.
     */
    template<typename Func>
    void run_in_sync_thread(DispatchContext &dc, Func qfunc) {
	dc.timer.expires_at(boost::posix_time::pos_infin);
	boost::asio::post(sync_ioc_,
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

    boost::uuids::random_generator *uuidgen() {
	auto gen = uuidgen_.get();
	if (gen == 0)
	{
	    gen = new boost::uuids::random_generator();
	    std::cerr << "creating new uuidgen " << gen << " in thread " << std::this_thread::get_id() << "\n";
	    uuidgen_.reset(gen);
	}
	return gen;
    }
};


#endif
