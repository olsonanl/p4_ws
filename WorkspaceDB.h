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

class WorkspaceDB;

class WorkspaceDBQuery
{
private:
    mongocxx::pool::entry  client_;
    std::shared_ptr<WorkspaceDB> db_;
public:
    WorkspaceDBQuery(mongocxx::pool::entry p, std::shared_ptr<WorkspaceDB> db)
	: client_(std::move(p))
	, db_(db) {
    }
    ~WorkspaceDBQuery() { std::cerr << "destroy WorkspaceDBQuery\n"; }
    WSPath parse_path(const boost::json::string &p);
    ObjectMeta lookup_object_meta(const WSPath &path);

};

class WorkspaceDB
    : public std::enable_shared_from_this<WorkspaceDB>
{
    std::string db_name_;
    mongocxx::uri uri_;
    mongocxx::pool pool_;
    mongocxx::instance instance_;

    boost::asio::thread_pool thread_pool_;

public:
    WorkspaceDB(const std::string &uri, int threads, const std::string &db_name);

    ~WorkspaceDB() { std::cerr << "destroy WorkspaceDB\n"; }

    std::unique_ptr<WorkspaceDBQuery> make_query();

    const std::string &db_name() { return db_name_; }
    boost::asio::thread_pool &thread_pool() { return thread_pool_; }

    template<typename Func>
    void run_in_thread(DispatchContext &dc, Func qfunc) {
	dc.timer.expires_at(boost::posix_time::pos_infin);
	boost::asio::post(thread_pool_,
			  [&dc, qfunc, this]() {
			      std::cerr << "in thread\n";
			      auto q = make_query();
			      qfunc(std::move(q));
			      std::cerr << "thread leaving\n";
			      dc.timer.cancel_one();
			  });
	
	boost::system::error_code ec({});
	dc.timer.async_wait(dc.yield[ec]);
	if (ec != boost::asio::error::operation_aborted)
	    std::cerr << "async_wait: " << ec.message() << "\n";

    }


};


#endif
