#include "OsInfo.h"
#include <sys/utsname.h>
namespace Common
{
std::string getOsInfo ()
{
    struct utsname u;
    if (0 != uname(&u)) return "unknown";
    return std::string(u.sysname) + " " + u.release + " " + u.machine;
}
}
