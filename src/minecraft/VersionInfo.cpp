#include "minecraft/VersionInfo.h"

#include <QtGlobal>

namespace lami {

QString currentOsName()
{
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "osx";
#else
    return "linux";
#endif
}

} // namespace lami
