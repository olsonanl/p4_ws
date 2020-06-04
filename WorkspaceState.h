#ifndef _WorkspaceState_h
#define _WorkspaceState_h

/*
 * Manage the global state required for the workspace service.
 */

class WorkspaceState : public std::enable_shared_from_this<WorkspaceState>
{
    std::string api_root_;
public:
    WorkspaceState()
	: api_root_("/api") {
    }

    const std::string &api_root() { return api_root_; }
    

    
};

#endif
