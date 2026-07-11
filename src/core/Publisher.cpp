#include "core/Publisher.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include <QDir>
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
        emit progress(QStringLiteral("Rôle OK (%1). Upload des mods dans la banque…")
                          .arg(roleToString(role)));
        uploadNextMod();  // démarre l'upload de la banque (ou passe au manifeste)
    });

    // Chaque mod uploadé (ou sauté car déjà présent) → mod suivant.
    connect(m_gh, &GitHubClient::uploadDone, this, [this](const QString &path, bool skipped) {
        if (skipped) ++m_skipped; else ++m_uploaded;
        emit progress(QStringLiteral("%1 %2").arg(skipped ? "déjà en banque" : "ajouté", path));
        uploadNextMod();
    });

    // Manifeste écrit → on met à jour l'index adresse→id (si adresse fournie),
    // sinon la publication est déjà terminée.
    connect(m_gh, &GitHubClient::filePut, this, [this](const QString &) {
        if (m_pending.address.isEmpty()) {
            emit published(m_pending.id);
            return;
        }
        emit progress(QStringLiteral("Manifeste écrit. Enregistrement de l'adresse « %1 »…")
                          .arg(m_pending.address));
        m_gh->upsertAddressIndex(m_pending.address, m_pending.id,
                                 QStringLiteral("Index : %1 -> %2 via LAMI")
                                     .arg(m_pending.address, m_pending.id));
    });

    // Index adresse→id à jour → publication terminée.
    connect(m_gh, &GitHubClient::indexUpdated, this, [this]() {
        emit published(m_pending.id);
    });

    connect(m_gh, &GitHubClient::writeError, this, &Publisher::failed);
    connect(m_gh, &GitHubClient::errorOccurred, this, &Publisher::failed);
}

void Publisher::publish(const ServerInfo &server, const QString &localModsDir,
                        const QString &publisherUuid)
{
    if (server.id.isEmpty()) {
        emit failed("Serveur sans id : publication impossible.");
        return;
    }
    m_pending      = server;
    m_localModsDir = localModsDir;
    m_uuid         = publisherUuid;
    m_uploaded = m_skipped = 0;
    m_modQueue.clear();
    for (const ModEntry &m : server.mods)
        m_modQueue.enqueue(m);

    // On (re)lit les rôles à la source avant toute écriture.
    m_gh->fetchRoles();
}

void Publisher::publishFromFolder(ServerInfo server, const QString &localModsDir,
                                  const QString &publisherUuid)
{
    server.mods = scanModsFolder(localModsDir);
    emit progress(QStringLiteral("Dossier scanné : %1 mod(s) détecté(s).")
                      .arg(server.mods.size()));
    publish(server, localModsDir, publisherUuid);
}

void Publisher::publishFromZip(const ServerInfo &server, const QString &zipPath,
                               const QString &publisherUuid)
{
    // Dossier temporaire dédié à cette extraction.
    const QString tmp = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath("lami-zip-" + server.id);
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);

    QString err;
    const QStringList jars = ModArchive::extractJars(zipPath, tmp, &err);
    if (jars.isEmpty()) {
        emit failed(err.isEmpty()
                        ? QStringLiteral("Aucun .jar trouvé dans le zip.")
                        : err);
        return;
    }
    emit progress(QStringLiteral("Zip extrait : %1 mod(s).").arg(jars.size()));
    publishFromFolder(server, tmp, publisherUuid);
}

void Publisher::uploadNextMod()
{
    if (m_modQueue.isEmpty()) {
        emit progress(QStringLiteral("Banque : %1 ajouté(s), %2 mutualisé(s). "
                                     "Écriture du manifeste…").arg(m_uploaded).arg(m_skipped));
        writeManifest();
        return;
    }

    const ModEntry mod = m_modQueue.dequeue();
    QFile f(QDir(m_localModsDir).filePath(mod.file));
    if (!f.open(QIODevice::ReadOnly)) {
        emit failed(QStringLiteral("Mod introuvable en local : %1").arg(mod.file));
        return;
    }
    const QByteArray content = f.readAll();

    m_gh->uploadIfAbsent(modBankPath(m_pending, mod), content,
                         QStringLiteral("Ajout du mod %1 (%2/%3) via LAMI")
                             .arg(mod.file, m_pending.minecraftVersion, m_pending.loader));
}

void Publisher::writeManifest()
{
    const QByteArray json =
        QJsonDocument(serverToJson(m_pending)).toJson(QJsonDocument::Indented);
    m_gh->putFile("servers/" + m_pending.id + ".json", json,
                  QStringLiteral("Publication du serveur %1 via LAMI").arg(m_pending.id));
}

} // namespace lami
