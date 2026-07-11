#include "core/Publisher.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

#include "core/ModArchive.h"
#include "core/ModScanner.h"
#include "github/GitHubClient.h"
#include "roles/Permissions.h"

namespace lami {

Publisher::Publisher(GitHubClient *gh, QObject *parent)
    : QObject(parent)
    , m_gh(gh)
{
    // Étape rôle : dès qu'on a roles.json, on décide.
    connect(m_gh, &GitHubClient::rolesFetched, this, [this](const RoleTable &roles) {
        const Role role = RoleResolver::roleFor(m_uuid, roles);
        if (!RoleResolver::can(role, Capability::PublishServer)) {
            emit denied(QStringLiteral("Rôle « %1 » insuffisant pour publier "
                                       "(Hébergeur ou Super Admin requis).")
                            .arg(roleToString(role)));
            return;
        }
        emit progress(QStringLiteral("Rôle OK (%1). Upload des fichiers dans la banque…")
                          .arg(roleToString(role)));
        uploadNext();
    });

    // Chaque fichier uploadé (ou sauté car déjà présent) → suivant.
    connect(m_gh, &GitHubClient::uploadDone, this, [this](const QString &path, bool skipped) {
        if (skipped) ++m_skipped; else ++m_uploaded;
        emit progress(QStringLiteral("%1 %2").arg(skipped ? "déjà en banque" : "ajouté", path));
        uploadNext();
    });

    // Manifeste écrit → mise à jour de l'index adresse→id (si adresse), sinon fini.
    connect(m_gh, &GitHubClient::filePut, this, [this](const QString &) {
        if (m_pending.address.isEmpty()) { emit published(m_pending.id); return; }
        emit progress(QStringLiteral("Manifeste écrit. Enregistrement de l'adresse « %1 »…")
                          .arg(m_pending.address));
        m_gh->upsertAddressIndex(m_pending.address, m_pending.id,
                                 QStringLiteral("Index : %1 -> %2 via LAMI")
                                     .arg(m_pending.address, m_pending.id));
    });
    connect(m_gh, &GitHubClient::indexUpdated, this, [this]() { emit published(m_pending.id); });

    connect(m_gh, &GitHubClient::writeError, this, &Publisher::failed);
    connect(m_gh, &GitHubClient::errorOccurred, this, &Publisher::failed);
}

void Publisher::publish(ServerInfo server, const QHash<QString, QString> &localDirs,
                        const QString &publisherUuid)
{
    if (server.id.isEmpty()) { emit failed("Serveur sans id : publication impossible."); return; }

    m_pending  = server;
    m_uuid     = publisherUuid;
    m_uploaded = m_skipped = 0;
    m_uploadQueue.clear();

    // Pour chaque catégorie : scanner le dossier → remplir la liste + file d'upload.
    for (const char *type : {assets::Mods, assets::Plugins, assets::ResourcePacks, assets::Shaders}) {
        if (!localDirs.contains(type))
            continue;
        const QString dir = localDirs.value(type);
        const QVector<ModEntry> entries = scanFolder(dir);   // tous les fichiers
        m_pending.assetList(type) = entries;
        for (const ModEntry &e : entries)
            m_uploadQueue.enqueue({QDir(dir).filePath(e.file), assetBankPath(m_pending, type, e)});
    }

    // On (re)lit les rôles à la source avant toute écriture.
    m_gh->fetchRoles();
}

void Publisher::publishFromZips(ServerInfo server, const QHash<QString, QString> &zips,
                                const QString &publisherUuid)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QHash<QString, QString> dirs;

    for (auto it = zips.begin(); it != zips.end(); ++it) {
        const QString type = it.key();
        const QString zip  = it.value();
        if (zip.isEmpty())
            continue;
        const QString tmp = QDir(base).filePath("lami-pub-" + server.id + "-" + type);
        QDir(tmp).removeRecursively();
        QDir().mkpath(tmp);
        QString err;
        const QStringList files = ModArchive::extract(zip, tmp, QString(), &err);  // tous fichiers
        if (files.isEmpty()) {
            emit failed(err.isEmpty() ? QStringLiteral("Zip « %1 » vide/illisible.").arg(type) : err);
            return;
        }
        emit progress(QStringLiteral("%1 : %2 fichier(s) extrait(s).").arg(type).arg(files.size()));
        dirs.insert(type, tmp);
    }

    if (dirs.isEmpty()) { emit failed("Aucun fichier à publier."); return; }
    publish(server, dirs, publisherUuid);
}

void Publisher::publishFromFolder(ServerInfo server, const QString &localModsDir,
                                  const QString &publisherUuid)
{
    publish(server, {{assets::Mods, localModsDir}}, publisherUuid);
}

void Publisher::publishFromZip(const ServerInfo &server, const QString &zipPath,
                               const QString &publisherUuid)
{
    publishFromZips(server, {{assets::Mods, zipPath}}, publisherUuid);
}

void Publisher::uploadNext()
{
    if (m_uploadQueue.isEmpty()) {
        emit progress(QStringLiteral("Banque : %1 ajouté(s), %2 mutualisé(s). Manifeste…")
                          .arg(m_uploaded).arg(m_skipped));
        writeManifest();
        return;
    }

    const Upload up = m_uploadQueue.dequeue();
    QFile f(up.localPath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit failed(QStringLiteral("Fichier introuvable : %1").arg(up.localPath));
        return;
    }
    m_gh->uploadIfAbsent(up.bankPath, f.readAll(),
                         QStringLiteral("Ajout de %1 via LAMI").arg(up.bankPath));
}

void Publisher::writeManifest()
{
    const QByteArray json =
        QJsonDocument(serverToJson(m_pending)).toJson(QJsonDocument::Indented);
    m_gh->putFile("servers/" + m_pending.id + ".json", json,
                  QStringLiteral("Publication du serveur %1 via LAMI").arg(m_pending.id));
}

} // namespace lami
