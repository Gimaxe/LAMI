#include "core/ModScanner.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

#include "sync/SyncManager.h"  // sha256File

namespace lami {

QVector<ModEntry> scanModsFolder(const QString &dir)
{
    QVector<ModEntry> mods;

    QDir d(dir);
    const QStringList jars = d.entryList({"*.jar"}, QDir::Files, QDir::Name);
    mods.reserve(jars.size());

    for (const QString &name : jars) {
        const QString abs = d.filePath(name);
        ModEntry m;
        m.file   = name;
        m.sha256 = SyncManager::sha256File(abs);
        m.size   = QFileInfo(abs).size();
        mods.push_back(m);
    }

    // Ordre stable (déjà trié par QDir::Name, mais on le garantit).
    std::sort(mods.begin(), mods.end(),
              [](const ModEntry &a, const ModEntry &b) { return a.file < b.file; });
    return mods;
}

} // namespace lami
