#ifndef _WorkspaceState_h
#define _WorkspaceState_h

/*
 * Manage the global state required for the workspace service.
 */

#include <memory>

#include "AuthToken.h"
#include "SigningCerts.h"

class ServiceDispatcher;
class WorkspaceDB;

class WorkspaceState : public std::enable_shared_from_this<WorkspaceState>
{
    std::string api_root_;
    bool quit_;
    std::shared_ptr<ServiceDispatcher> dispatcher_;
    std::shared_ptr<WorkspaceDB> db_;
    SigningCerts certs_;

public:
    WorkspaceState(std::shared_ptr<ServiceDispatcher> dispatcher,
		   std::shared_ptr<WorkspaceDB> db
	)
	: api_root_("/api")
	, quit_(false)
	, dispatcher_(dispatcher)
	, db_(db) {
    }
    ~WorkspaceState() { std::cerr << "destroy  WorkspaceState\n"; }

    const std::string &api_root() { return api_root_; }
    bool quit() { return quit_; }
    void quit(bool v) { quit_ = v; }

    std::shared_ptr<ServiceDispatcher> dispatcher() { return dispatcher_; }

    std::shared_ptr<WorkspaceDB> db() { return db_; }

    const SigningCerts &signing_certs () const { return certs_; }
    bool validate_certificate(const AuthToken &tok) { return certs_.validate(tok); }
};

#endif
