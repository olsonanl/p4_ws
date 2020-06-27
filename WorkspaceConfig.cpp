#include "WorkspaceConfig.h"
#include "WorkspaceService.h"

std::string WorkspaceConfig::filesystem_path_for_object(const WSPath &obj)
{
    std::string res = filesystem_base();
    res += "/P3WSDB/";
    res += obj.workspace.owner;
    res += "/";
    res += obj.workspace.name;
    res += "/";
    res += obj.path;
    res += "/";
    res += obj.name;
    return res;
}
