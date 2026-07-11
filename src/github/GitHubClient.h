#pragma once

#include <QObject>
#include <QString>

#include "github/Models.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace lami {

// Client de lecture du repo-BDD GitHub (voir src/github/README.md).
// Pour l'instant : lecture seule via les fichiers "raw" (aucun token requis
// tant que le repo est public). L'écriture (Hébergeur/Admin, API contents)
// viendra dans un second temps.
//
// Usage :
//   auto *gh = new GitHubClient("Atraxe", "lami-db", "main", this);
//   connect(gh, &GitHubClient::serverFetched, ...);
//   connect(gh, &GitHubClient::errorOccurred, ...);
//   gh->fetchServer("atraxe-smp");
class GitHubClient : public QObject
{
    Q_OBJECT

public:
    GitHubClient(QString owner,
                 QString repo,
                 QString branch = "main",
                 QObject *parent = nullptr);

    // Token GitHub en lecture (repo privé). Si vide, le client suppose un repo
    // public et lit via raw.githubusercontent.com. Sinon il passe par l'API
    // authentifiée (Authorization: Bearer ...). Voir src/github/README.md.
    void setToken(const QString &token);

    // URL "raw" d'un fichier du repo (repo public), ex. "servers/atraxe-smp.json".
    QString rawUrl(const QString &path) const;

    // URL API "contents" d'un fichier avec ?ref=branch (lecture, repo privé).
    QString apiContentsUrl(const QString &path) const;
    // URL API "contents" sans ?ref (écriture : la branche va dans le corps).
    QString apiContentsUrlNoRef(const QString &path) const;

    // Récupère et parse servers/<id>.json → signal serverFetched / errorOccurred.
    void fetchServer(const QString &id);

    // Récupère et parse roles.json → signal rolesFetched / errorOccurred.
    void fetchRoles();

    // Résout une adresse (IP / sous-domaine) via servers/index.json puis charge
    // le serveur correspondant → signal serverFetched / errorOccurred.
    void fetchServerByAddress(const QString &address);

    // Liste puis charge TOUS les serveurs du repo (servers/*.json sauf index).
    // → signal serversFetched / errorOccurred.
    void fetchAllServers();

    // --- Écriture (nécessite un token avec Contents: Read and write) ---
    // Crée ou met à jour un fichier (récupère le sha existant automatiquement).
    void putFile(const QString &path, const QByteArray &content,
                 const QString &commitMessage);
    // Supprime un fichier (récupère son sha automatiquement).
    void deleteFile(const QString &path, const QString &commitMessage);

    // Upload SEULEMENT si le fichier n'existe pas déjà (dédoublonnage banque).
    // Émet uploadDone(path, skipped) : skipped=true si déjà présent.
    void uploadIfAbsent(const QString &path, const QByteArray &content,
                        const QString &commitMessage);

    // Ajoute/met à jour l'entrée adresse→id dans servers/index.json (lit, fusionne,
    // réécrit). Émet indexUpdated / writeError.
    void upsertAddressIndex(const QString &address, const QString &serverId,
                            const QString &commitMessage);

    // Administration des rôles (roles.json). Émettent rolesUpdated / writeError.
    void setRole(const QString &uuid, const QString &role, const QString &commitMessage);
    void removeRole(const QString &uuid, const QString &commitMessage);

signals:
    void serverFetched(const lami::ServerInfo &server);
    void serversFetched(const QVector<lami::ServerInfo> &servers);
    void rolesFetched(const lami::RoleTable &roles);
    void errorOccurred(const QString &message);
    void filePut(const QString &path);
    void fileDeleted(const QString &path);
    void uploadDone(const QString &path, bool skipped);
    void indexUpdated();
    void rolesUpdated();
    void writeError(const QString &message);

private:
    QNetworkReply *get(const QString &path);

    QNetworkAccessManager *m_net;
    QString m_owner;
    QString m_repo;
    QString m_branch;
    QString m_token;  // vide => repo public (raw), sinon API authentifiée
};

} // namespace lami
