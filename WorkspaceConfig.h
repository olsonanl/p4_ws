#ifndef _WorkspaceConfig_h
#define _WorkspaceConfig_h

#include "ServiceConfig.h"
#include <set>
#include <vector>
#include <boost/algorithm/string.hpp>

class WSPath;

class WorkspaceConfig
    : public ServiceConfig
{
    std::set<std::string> admins_;
    std::string filesystem_base_;
    unsigned long download_lifetime_;
    std::string download_url_base_;
    std::string mongodb_dbname_;
    std::string mongodb_url_;
    int mongodb_client_threads_;
public:
    WorkspaceConfig()
	: ServiceConfig("Workspace")
	, download_lifetime_(3600)
	, mongodb_client_threads_(1) {
    }

    bool parse() {
	if (!ServiceConfig::parse())
	    return false;

	std::vector<std::string> admin_list;
	boost::algorithm::split(admin_list, get_string("adminlist"), boost::is_any_of(";"));
	std::copy(admin_list.begin(), admin_list.end(), std::inserter(admins_, admins_.begin()));

	for (auto x: admins_)
	    std::cerr << "admin: " << x << "\n";

	filesystem_base_ = get_string("db-path");

	download_lifetime_ = get_long("download-lifetime", download_lifetime_);
	download_url_base_ = get_string("download-url-base", "");
	mongodb_url_ = get_string("mongodb-host", "");
	mongodb_dbname_ = get_string("mongodb-database", "");
	mongodb_client_threads_ = get_long("mongodb-client-threads", 1);
       
	return true;
    }

    bool user_is_admin(const std::string &user) const {
	return admins_.find(user) != admins_.end();
    }
    const std::string &filesystem_base() const { return filesystem_base_; }

    std::string filesystem_path_for_object(const WSPath &obj);
    unsigned long download_lifetime() const { return download_lifetime_; }
    const std::string &download_url_base() const { return download_url_base_; }
    const std::string &mongodb_url() const { return mongodb_url_; }
    const std::string &mongodb_dbname() const { return mongodb_dbname_; }
    int mongodb_client_threads() const { return mongodb_client_threads_; }
};


#endif
