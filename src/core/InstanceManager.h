#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "auth/MicrosoftAuth.h"        // MinecraftSession
#include "github/Models.h"            // ServerInfo
#include "minecraft/Downloader.h"     // DownloadTask
#include "minecraft/VersionInfo.h"    // VersionInfo

class QNetworkAccessManager;

namespace lami {

class GitHubClient;
class MojangMeta;
class FabricMeta;
class JavaProvisioner;
class ForgeInstaller;
struct FabricProfile;
struct ForgeProfile;

// Plan complet pour préparer et lancer une instance de serveur.
struct LaunchPlan {
    ServerInfo  server;
    VersionInfo version;
    QVector<DownloadTask> downloads;   // client + libraries + assets + mods du repo
    QStringList toDeleteMods;          // mods retirés du manifeste (sync non-destructif)
    QStringList launchCommand;         // [java, jvmArgs, mainClass, gameArgs]
    QString     gameDir;
    bool        valid = false;
};

// Orchestrateur : à partir d'un id de serveur et d'une session Minecraft,
// enchaîne github (manifeste serveur) → Mojang (version + assets) → sync
// (mods du repo, non-destructif) et produit un LaunchPlan complet.
//
// plan() ne télécharge RIEN (petits appels réseau : manifeste, JSON de version,
// index d'assets). L'exécution = Downloader sur plan.downloads, puis QProcess
// sur plan.launchCommand. Séparer plan/exécution rend le tout testable headless.
class InstanceManager : public QObject
{
    Q_OBJECT

public:
    InstanceManager(QString owner, QString repo, QString branch, QString token,
                    QString dataRoot, QString javaPath, QObject *parent = nullptr);

    // Construit le plan pour ce serveur (par id) avec cette session.
    void plan(const QString &serverId, const MinecraftSession &session);

    // Idem mais résout d'abord l'adresse (IP/sous-domaine) via servers/index.json.
    void planByAddress(const QString &address, const MinecraftSession &session);

    // Mot de passe fourni par le joueur (vérifié contre le hash du serveur).
    void setPassword(const QString &password) { m_password = password; }

    // Active/désactive la vérification du mot de passe (activée par défaut).
    // Le lancement d'un serveur DÉJÀ installé la désactive (mdp demandé à l'install).
    void setVerifyPassword(bool on) { m_verifyPassword = on; }

    // Force l'utilisation du java fourni (javaPath) au lieu de provisionner un JRE.
    void setForceJava(bool on) { m_forceJava = on; }

signals:
    void progress(const QString &step);
    void planReady(const lami::LaunchPlan &plan);
    void failed(const QString &message);

private:
    void onServerFetched(const ServerInfo &server);
    void onVersionResolved(const VersionInfo &version);
    void onFabricResolved(const FabricProfile &profile);
    void onForgeResolved(const ForgeProfile &profile);
    void provisionJavaThenAssets();
    void afterJavaReady();
    void fetchAssetIndex();
    void assemblePlan(const QByteArray &assetIndexJson);

    // Disposition des dossiers.
    QString librariesRoot() const;
    QString assetsRoot() const;
    QString versionDir(const QString &id) const;
    QString clientJar(const QString &id) const;
    QString nativesDir(const QString &id) const;
    QString instanceDir(const QString &serverId) const;

    GitHubClient *m_gh;
    MojangMeta   *m_meta;
    FabricMeta   *m_fabric;
    JavaProvisioner *m_java;
    ForgeInstaller  *m_forge;     // installeur Forge/NeoForge (créé à la demande)
    QNetworkAccessManager *m_net;
    QString m_resolvedJavaPath;   // JRE provisionné (sinon repli sur m_javaPath)
    QString m_password;           // mot de passe saisi par le joueur
    bool    m_verifyPassword = true;  // vérifier le mdp ? (false au lancement installé)
    bool    m_forceJava = false;      // utiliser m_javaPath au lieu du JRE auto ?
    bool    m_forgeDone = false;  // profil Forge déjà fusionné ?

    QString m_token;
    QString m_dataRoot;
    QString m_javaPath;

    // État de l'opération en cours.
    MinecraftSession m_session;
    ServerInfo       m_server;
    VersionInfo      m_version;
};

} // namespace lami
