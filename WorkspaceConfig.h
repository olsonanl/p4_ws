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
public:
    WorkspaceConfig()
	: ServiceConfig("Workspace")
	, download_lifetime_(3600) {
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
       
	return true;
    }

    bool user_is_admin(const std::string &user) const {
	return admins_.find(user) != admins_.end();
    }
    const std::string filesystem_base() const { return filesystem_base_; }

    std::string filesystem_path_for_object(const WSPath &obj);
    unsigned long download_lifetime() { return download_lifetime_; }
};


#endif
