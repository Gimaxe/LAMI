#include "core/ModScanner.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

#include "sync/SyncManager.h"  // sha256File

namespace lami {

QVector<ModEntry> scanFolder(const QString &dir, const QStringList &filters)
{
    QVector<ModEntry> out;

    QDir d(dir);
    const QStringList names = d.entryList(filters, QDir::Files, QDir::Name);
    out.reserve(names.size());

    for (const QString &name : names) {
        const QString abs = d.filePath(name);
        ModEntry m;
        m.file   = name;
        m.sha256 = SyncManager::sha256File(abs);
        m.size   = QFileInfo(abs).size();
        out.push_back(m);
    }

    std::sort(out.begin(), out.end(),
              [](const ModEntry &a, const ModEntry &b) { return a.file < b.file; });
    return out;
}

QVector<ModEntry> scanModsFolder(const QString &dir)
{
    return scanFolder(dir, {"*.jar"});
}

} // namespace lami
