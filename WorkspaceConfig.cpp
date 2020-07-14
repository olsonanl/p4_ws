#include "WorkspaceService.h"
#include "WorkspaceConfig.h"

boost::filesystem::path WorkspaceConfig::filesystem_path_for_object(const WSPath &obj)
{
    boost::filesystem::path res = filesystem_base() / "P3WSDB"
	/ obj.workspace.owner / obj.workspace.name / obj.path / obj.name;
    return res;
}

boost::filesystem::path WorkspaceConfig::filesystem_path_for_workspace(const WSWorkspace &ws)
{
    boost::filesystem::path res = filesystem_base() / "P3WSDB" / ws.owner / ws.name;
    return res;
}

