#pragma once

#include <QObject>
#include <QQueue>
#include <QString>

#include "github/Models.h"

namespace lami {

class GitHubClient;

// Publie/met à jour un serveur dans le repo-BDD, APRÈS vérification du rôle.
//
// Étapes : (1) lire roles.json et exiger PublishServer (Hébergeur/Super Admin) ;
// (2) uploader chaque mod dans la BANQUE mutualisée mods/<version>/<loader>/<file>
//     en sautant ceux déjà présents (dédoublonnage) ; (3) écrire le manifeste
//     servers/<id>.json qui référence ces mods.
//
// ⚠️ Vérification d'identité renforcée (conception) : l'UUID doit avoir été
// re-confirmé via une vraie session Microsoft juste avant. Supposé fait ici.
class Publisher : public QObject
{
    Q_OBJECT

public:
    Publisher(GitHubClient *gh, QObject *parent = nullptr);

    // Publie `server` : les fichiers de mods sont lus dans `localModsDir`
    // (leur nom = ModEntry::file), au nom de l'utilisateur `publisherUuid`.
    void publish(const ServerInfo &server, const QString &localModsDir,
                 const QString &publisherUuid);

    // Version clé en main : scanne `localModsDir` pour construire la liste des
    // mods (nom + sha256 + taille), puis publie. L'Hébergeur ne fournit que les
    // infos du serveur (id/adresse/version/loader) + son dossier de mods.
    void publishFromFolder(ServerInfo server, const QString &localModsDir,
                           const QString &publisherUuid);

    // Comme publishFromFolder mais depuis un .zip de mods : on extrait les .jar
    // dans un dossier temporaire puis on publie. Émet failed si le zip est illisible.
    void publishFromZip(const ServerInfo &server, const QString &zipPath,
                        const QString &publisherUuid);

signals:
    void progress(const QString &step);
    void published(const QString &serverId);
    void denied(const QString &reason);      // rôle insuffisant
    void failed(const QString &message);     // erreur technique

private:
    void uploadNextMod();
    void writeManifest();

    GitHubClient *m_gh;
    ServerInfo    m_pending;
    QString       m_localModsDir;
    QString       m_uuid;
    QQueue<ModEntry> m_modQueue;
    int m_uploaded = 0;
    int m_skipped  = 0;
};

} // namespace lami
