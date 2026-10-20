#include "BuildInfo.h"

// All four come from the CMake configure step (see Client/qt/CMakeLists.txt). The defaults
// below keep the file compilable on its own and describe a build we cannot identify.
#ifndef AURAL_VERSION
#define AURAL_VERSION "unknown"
#endif
#ifndef AURAL_BUILD_REVISION
#define AURAL_BUILD_REVISION "unknown"
#endif
#ifndef AURAL_BUILD_BRANCH
#define AURAL_BUILD_BRANCH "unknown"
#endif
#ifndef AURAL_BUILD_MODIFIED
#define AURAL_BUILD_MODIFIED 0
#endif

QString BuildInfo::version() { return QStringLiteral(AURAL_VERSION); }
QString BuildInfo::revision() { return QStringLiteral(AURAL_BUILD_REVISION); }
QString BuildInfo::branch() { return QStringLiteral(AURAL_BUILD_BRANCH); }
bool BuildInfo::modified() { return AURAL_BUILD_MODIFIED != 0; }

QString BuildInfo::summary()
{
    QString out = version();
    if (revision() == QLatin1String("unknown"))
        return out + QStringLiteral(" (source revision unknown)");

    out += QStringLiteral(" (") + revision();
    if (branch() != QLatin1String("unknown"))
        out += QStringLiteral(" on ") + branch();
    if (modified())
        out += QStringLiteral(", modified");
    return out + QStringLiteral(")");
}
