#include "WorkspaceService.h"
#include "WorkspaceConfig.h"

boost::filesystem::path WorkspaceConfig::filesystem_path_for_object(const WSPath &obj)
{
    boost::filesystem::path res = filesystem_base() / "P3WSDB"
	/ obj.workspace.owner / obj.workspace.name / obj.path / obj.name;
    return res;
}
