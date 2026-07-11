#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

#include "github/Models.h"

namespace lami {

class GitHubClient;

// Publie/met à jour un serveur dans le repo-BDD, APRÈS vérification du rôle.
//
// Étapes : (1) lire roles.json et exiger PublishServer (Hébergeur/Super Admin) ;
// (2) uploader chaque asset (mods, plugins, resource packs, shaders) dans la
//     BANQUE mutualisée à son emplacement (voir assetBankPath) en sautant ceux
//     déjà présents (dédoublonnage) ; (3) écrire le manifeste servers/<id>.json.
//
// ⚠️ L'UUID doit avoir été confirmé via une vraie session Microsoft (anti-usurpation).
class Publisher : public QObject
{
    Q_OBJECT

public:
    Publisher(GitHubClient *gh, QObject *parent = nullptr);

    // Publie `server`. `localDirs` : catégorie d'asset (assets::*) -> dossier local
    // contenant les fichiers de cette catégorie. Les listes d'assets du serveur
    // sont (re)construites en scannant ces dossiers.
    void publish(ServerInfo server, const QHash<QString, QString> &localDirs,
                 const QString &publisherUuid);

    // Comme publish mais depuis des .zip par catégorie (extraits en temporaire).
    void publishFromZips(ServerInfo server, const QHash<QString, QString> &zips,
                         const QString &publisherUuid);

    // Raccourcis mods uniquement (compat / tests).
    void publishFromFolder(ServerInfo server, const QString &localModsDir,
                           const QString &publisherUuid);
    void publishFromZip(const ServerInfo &server, const QString &zipPath,
                        const QString &publisherUuid);

signals:
    void progress(const QString &step);
    void published(const QString &serverId);
    void denied(const QString &reason);      // rôle insuffisant
    void failed(const QString &message);     // erreur technique

private:
    struct Upload { QString localPath; QString bankPath; };

    void uploadNext();
    void writeManifest();

    GitHubClient *m_gh;
    ServerInfo    m_pending;
    QString       m_uuid;
    QQueue<Upload> m_uploadQueue;
    int m_uploaded = 0;
    int m_skipped  = 0;
};

} // namespace lami
