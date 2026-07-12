#include "sync/SyncManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <utility>

namespace lami {

SyncManager::SyncManager(QString instanceDir)
    : m_instanceDir(std::move(instanceDir))
{
}

QString SyncManager::absPath(const QString &relativePath) const
{
    return QDir(m_instanceDir).filePath(relativePath);
}

QString SyncManager::registryPath() const
{
    return QDir(m_instanceDir).filePath(".lami/installed.json");
}

QString SyncManager::sha256File(const QString &absolutePath)
{
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))   // lit le fichier par blocs en interne
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QStringList SyncManager::loadInstalled() const
{
    QFile f(registryPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};  // pas encore de registre = rien d'installé par le launcher

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object().value("installed").toArray();

    QStringList paths;
    paths.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QString p = v.toString();
        if (!p.isEmpty())
            paths << p;
    }
    return paths;
}

bool SyncManager::saveInstalled(const QStringList &relativePaths) const
{
    QDir().mkpath(QFileInfo(registryPath()).absolutePath());

    QJsonArray arr;
    // Tri + dédoublonnage pour un registre stable et lisible.
    QStringList sorted = relativePaths;
    sorted.removeDuplicates();
    sorted.sort();
    for (const QString &p : sorted)
        arr.append(p);

    QJsonObject root;
    root.insert("installed", arr);

    QFile f(registryPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

SyncPlan SyncManager::computePlan(const ServerInfo &server) const
{
    SyncPlan plan;

    QSet<QString> manifestPaths;

    // 1) Ce que le manifeste demande, pour CHAQUE catégorie (mods, plugins,
    //    resourcepacks, shaders) : à télécharger (manquant / hash différent) ou
    //    déjà bon. Chaque asset est copié dans <instance>/<dossier>/<file>.
    for (const char *type : {assets::Mods, assets::Plugins,
                             assets::ResourcePacks, assets::Shaders}) {
        for (const ModEntry &asset : server.assetList(type)) {
            const QString rel = assetLocalPath(type, asset);
            manifestPaths.insert(rel);
            const QString local = absPath(rel);
            const AssetRef ref{type, asset};

            if (!QFileInfo::exists(local)) {
                plan.toDownload.push_back(ref);
                continue;
            }
            const QString localHash = sha256File(local);
            if (localHash.compare(asset.sha256, Qt::CaseInsensitive) == 0)
                plan.upToDate.push_back(ref);
            else
                plan.toDownload.push_back(ref);
        }
    }

    // 2) Suppressions PRUDENTES : uniquement des fichiers que le launcher a lui-
    //    même installés et qui ne sont plus au manifeste. Les fichiers inconnus
    //    du registre (ajouts manuels du joueur) ne sont jamais touchés.
    for (const QString &installed : loadInstalled()) {
        if (!manifestPaths.contains(installed) && QFileInfo::exists(absPath(installed)))
            plan.toDelete.push_back(installed);
    }

    return plan;
}

int SyncManager::applyDeletions(const SyncPlan &plan) const
{
    if (plan.toDelete.isEmpty())
        return 0;

    const QSet<QString> toDelete(plan.toDelete.begin(), plan.toDelete.end());

    int removed = 0;
    for (const QString &rel : plan.toDelete) {
        if (QFile::remove(absPath(rel)))
            ++removed;
    }

    // Le registre ne garde que ce qui reste géré par le launcher.
    QStringList remaining;
    for (const QString &p : loadInstalled()) {
        if (!toDelete.contains(p))
            remaining << p;
    }
    saveInstalled(remaining);

    return removed;
}

bool SyncManager::markInstalled(const QStringList &relativePaths) const
{
    QStringList all = loadInstalled();
    all += relativePaths;
    return saveInstalled(all);
}

} // namespace lami
