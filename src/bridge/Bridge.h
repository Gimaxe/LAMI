#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QProcess;
class QNetworkAccessManager;

#include "auth/MicrosoftAuth.h"  // MinecraftSession
#include "github/Models.h"      // RoleTable

namespace lami {

class GitHubClient;

// Pont entre l'UI (JS) et le backend C++. Indépendant du transport (WebSocket)
// et de la webview → testable en headless.
//
// Protocole : l'UI envoie une requête { id, method, params }. Le pont répond
// avec response({ id, ok, result | error }). Des événements non sollicités
// (progression de téléchargement, etc.) sont poussés via event({ event, ... }).
class Bridge : public QObject
{
    Q_OBJECT

public:
    explicit Bridge(QObject *parent = nullptr);

    // Traite une requête JSON de l'UI. Émet response() quand c'est prêt.
    void handle(const QJsonObject &request);

signals:
    void response(const QJsonObject &message);  // réponse à une requête (avec id)
    void event(const QJsonObject &message);      // notification poussée (sans id)

private:
    // Méthodes exposées à l'UI.
    void resolveServer(int id, const QJsonObject &params);
    void listMcVersions(int id);
    void listLoaderVersions(int id, const QJsonObject &params);
    void listServers(int id);
    void listInstalled(int id);
    void login(int id, const QJsonObject &params);
    void silentLogin(int id);                            // reconnexion via refresh token
    void logout(int id);                                 // efface la session mémorisée
    void resolveRoleAndReply(int id, const lami::MinecraftSession &s);   // rôle (Worker/repli) → réponse
    void devLogin(int id, const QJsonObject &params);   // PROVISOIRE (avant approbation Azure)
    void startDownload(int id, const QJsonObject &params);
    void cancelDownload(int id, const QJsonObject &params);
    void launch(int id, const QJsonObject &params);
    void stopGame(int id, const QJsonObject &params);
    void checkUpdate(int id, const QJsonObject &params);
    void installUpdate(int id, const QJsonObject &params);
    bool writeAndRunUpdater(const QString &installDir, const QString &archive,
                            const QString &staging);
    void openUrl(int id, const QJsonObject &params);
    void uninstall(int id, const QJsonObject &params);
    void openInstanceFolder(int id, const QJsonObject &params);
    void getSettings(int id);
    void setToken(int id, const QJsonObject &params);
    void listBackgrounds(int id);
    void saveSettings(int id, const QJsonObject &params);
    void publishServer(int id, const QJsonObject &params);
    void editServer(int id, const QJsonObject &params);
    void deleteServer(int id, const QJsonObject &params);
    void listRoles(int id);
    void setRole(int id, const QJsonObject &params);
    void removeRole(int id, const QJsonObject &params);

    // --- Worker de confiance : quand workerUrl est défini, les écritures passent
    // par lui (identité vérifiée via Mojang, aucun token GitHub côté client). ---
    bool useWorker() const;
    void postToWorker(int id, const QString &path, QJsonObject body);
    // Aplati les zips en liste plate [{type,file,base64,sha256,size}] et remplit
    // *assetListsOut avec les seules métadonnées (file/sha256/size) par catégorie.
    QJsonArray flattenAssetsForWorker(const QHash<QString, QString> &zips,
                                      QJsonObject *assetListsOut);
    // Publish/edit CHUNKÉ : uploade les assets par lots via /upload (pour ne pas
    // dépasser les limites du Worker), puis envoie finalPath+finalBody (assetLists).
    void publishChunked(int id, const QString &finalPath, QJsonObject finalBody,
                        const QString &mcVersion, const QString &loader,
                        const QHash<QString, QString> &zips);

    // Exécute `action(roles)` seulement si la session est Super Admin (sinon replyError).
    void requireSuperAdmin(int id, std::function<void(const RoleTable &)> action);

    // Helpers de réponse.
    void replyOk(int id, const QJsonObject &result);
    void replyError(int id, const QString &message);

    GitHubClient    *m_gh;
    QNetworkAccessManager *m_net;
    MicrosoftAuth   *m_auth = nullptr;
    MinecraftSession m_session;   // session authentifiée (source de vérité de l'UUID)
    QHash<QString, QProcess *> m_running;   // jeux en cours, par id de serveur

    // Téléchargements en cours, par id de serveur : permet une VRAIE annulation
    // (pendant la planification via mgr, pendant les transferts via dl).
    struct ActiveDownload {
        QPointer<QObject> mgr;   // InstanceManager (phase de planification)
        QPointer<QObject> dl;    // Downloader (phase de transfert)
        int requestId = -1;      // id de la requête startDownload (pour lui répondre à l'annulation)
    };
    QHash<QString, ActiveDownload> m_downloads;
};

// Sérialise un ServerInfo au format attendu par l'UI (mods/plugins/... + loader lisible).
class ServerInfo;
QJsonObject serverToUiJson(const ServerInfo &s);

} // namespace lami
