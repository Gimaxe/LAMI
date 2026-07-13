#include "core/InstanceManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <utility>

#include "github/GitHubClient.h"
#include "minecraft/FabricMeta.h"
#include "minecraft/ForgeInstaller.h"
#include "minecraft/JavaProvisioner.h"
#include "minecraft/LaunchBuilder.h"
#include "minecraft/MojangMeta.h"
#include "sync/SyncManager.h"

namespace lami {

namespace {
const char *kResourcesBase = "https://resources.download.minecraft.net/";

// Clé d'artefact Maven (group:artifact[:classifier]) sans la version, pour
// dédoublonner. "org.apache.commons:commons-lang3:3.13.0" -> "org.apache.commons:commons-lang3".
QString artifactKey(const Library &lib)
{
    const QStringList p = lib.name.split(':');
    if (p.size() < 2)
        return lib.path.isEmpty() ? lib.name : lib.path;
    QString key = p.value(0) + ":" + p.value(1);
    if (p.size() >= 4)                 // classifier (ex. natives-linux)
        key += ":" + p.value(3);
    return key;
}

// Supprime les libs en double par artefact (garde la 1re occurrence). Les libs du
// loader étant placées en tête, ce sont leurs versions qui priment sur vanilla.
// Indispensable pour Forge/NeoForge : BootstrapLauncher plante sur un classpath
// contenant deux fois le même jar (IllegalStateException: Duplicate key).
void dedupeLibraries(QVector<Library> &libs)
{
    QVector<Library> out;
    QSet<QString> seen;
    for (const Library &lib : libs) {
        const QString key = artifactKey(lib);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out << lib;
    }
    libs = out;
}
}

InstanceManager::InstanceManager(QString owner, QString repo, QString branch,
                                 QString token, QString dataRoot, QString javaPath,
                                 QObject *parent)
    : QObject(parent)
    , m_gh(new GitHubClient(std::move(owner), std::move(repo), std::move(branch), this))
    , m_meta(new MojangMeta(this))
    , m_fabric(new FabricMeta(this))
    , m_java(nullptr)
    , m_forge(nullptr)
    , m_net(new QNetworkAccessManager(this))
    , m_token(std::move(token))
    , m_dataRoot(std::move(dataRoot))
    , m_javaPath(std::move(javaPath))
{
    if (!m_token.isEmpty())
        m_gh->setToken(m_token);

    connect(m_gh, &GitHubClient::serverFetched, this, &InstanceManager::onServerFetched);
    connect(m_gh, &GitHubClient::errorOccurred, this, &InstanceManager::failed);
    connect(m_meta, &MojangMeta::resolved, this, &InstanceManager::onVersionResolved);
    connect(m_meta, &MojangMeta::errorOccurred, this, &InstanceManager::failed);
    connect(m_fabric, &FabricMeta::resolved, this, &InstanceManager::onFabricResolved);
    connect(m_fabric, &FabricMeta::errorOccurred, this, &InstanceManager::failed);

    m_java = new JavaProvisioner(m_dataRoot, this);
    connect(m_java, &JavaProvisioner::progress, this, &InstanceManager::progress);
    connect(m_java, &JavaProvisioner::ready, this, [this](const QString &javaPath) {
        m_resolvedJavaPath = javaPath;
        afterJavaReady();
    });
    connect(m_java, &JavaProvisioner::errorOccurred, this, [this](const QString &e) {
        // Java auto indisponible → on tente le java du système (repli).
        emit progress("Java auto indisponible (" + e + "). Utilisation du java système.");
        m_resolvedJavaPath = m_javaPath;
        afterJavaReady();
    });
}

// --- Disposition des dossiers ----------------------------------------------
QString InstanceManager::librariesRoot() const { return QDir(m_dataRoot).filePath("libraries"); }
QString InstanceManager::assetsRoot() const    { return QDir(m_dataRoot).filePath("assets"); }
QString InstanceManager::versionDir(const QString &id) const
{ return QDir(m_dataRoot).filePath("versions/" + id); }
QString InstanceManager::clientJar(const QString &id) const
{ return QDir(versionDir(id)).filePath(id + ".jar"); }
QString InstanceManager::nativesDir(const QString &id) const
{ return QDir(versionDir(id)).filePath("natives"); }
QString InstanceManager::instanceDir(const QString &serverId) const
{ return QDir(m_dataRoot).filePath("instances/" + serverId); }

// --- Enchaînement -----------------------------------------------------------
void InstanceManager::plan(const QString &serverId, const MinecraftSession &session)
{
    m_session = session;
    emit progress("Lecture du serveur depuis le repo…");
    m_gh->fetchServer(serverId);
}

void InstanceManager::planByAddress(const QString &address, const MinecraftSession &session)
{
    m_session = session;
    emit progress(QStringLiteral("Résolution de l'adresse « %1 »…").arg(address));
    m_gh->fetchServerByAddress(address);  // → serverFetched → suite identique
}

void InstanceManager::onServerFetched(const ServerInfo &server)
{
    m_server = server;
    emit progress(QString("Serveur « %1 » (Minecraft %2, %3). Résolution de la version…")
                      .arg(server.name, server.minecraftVersion, server.loader));
    m_meta->resolve(server.minecraftVersion);
}

void InstanceManager::onVersionResolved(const VersionInfo &version)
{
    m_version = version;

    // Mod loader : Fabric et Quilt utilisent le même format d'API meta.
    const QString loader = m_server.loader.trimmed().toLower();
    if (loader == "fabric" || loader == "quilt") {
        emit progress(QString("Version vanilla OK. Résolution du profil %1…")
                          .arg(loader == "quilt" ? "Quilt" : "Fabric"));
        const QString base = (loader == "quilt")
            ? FabricMeta::quiltBase() : FabricMeta::fabricBase();
        m_fabric->resolve(m_server.minecraftVersion, m_server.loaderVersion, base);
        return;
    }
    // Forge/NeoForge : l'installeur officiel (qui patche le client via ses
    // processors) a besoin de Java. On provisionne Java d'abord, puis on lance
    // l'installeur dans afterJavaReady(), et on fusionne le profil produit.
    if (loader == "forge" || loader == "neoforge") {
        emit progress(QString("Version vanilla OK. Préparation de %1…")
                          .arg(loader == "neoforge" ? "NeoForge" : "Forge"));
        provisionJavaThenAssets();
        return;
    }
    // vanilla (ou "" / valeur inconnue traitée comme vanilla)
    provisionJavaThenAssets();
}

void InstanceManager::afterJavaReady()
{
    const QString loader = m_server.loader.trimmed().toLower();
    if ((loader == "forge" || loader == "neoforge") && !m_forgeDone) {
        if (!m_forge) {
            m_forge = new ForgeInstaller(m_dataRoot, m_resolvedJavaPath, this);
            connect(m_forge, &ForgeInstaller::progress, this, &InstanceManager::progress);
            connect(m_forge, &ForgeInstaller::resolved, this, &InstanceManager::onForgeResolved);
            connect(m_forge, &ForgeInstaller::errorOccurred, this, &InstanceManager::failed);
        }
        m_forge->resolve(m_server.minecraftVersion, loader, m_server.loaderVersion);
        return;
    }
    fetchAssetIndex();
}

void InstanceManager::onForgeResolved(const ForgeProfile &profile)
{
    // Fusion Forge/NeoForge par-dessus la version vanilla : mainClass remplacée,
    // libraries ajoutées avant les vanilla, arguments JVM et jeu complétés.
    m_version.mainClass = profile.mainClass;
    m_version.libraries = profile.libraries + m_version.libraries;
    m_version.jvmArgs  += profile.jvmArgs;
    m_version.gameArgs += profile.gameArgs;
    // Le loader et vanilla partagent des libs (commons-lang3, guava…) : sans
    // dédoublonnage, BootstrapLauncher plante (Duplicate key). On garde la
    // version du loader (placée en tête).
    dedupeLibraries(m_version.libraries);
    m_forgeDone = true;

    emit progress(QString("Loader fusionné (%1 lib(s), mainClass %2).")
                      .arg(m_version.libraries.size())
                      .arg(profile.mainClass));
    fetchAssetIndex();
}

void InstanceManager::provisionJavaThenAssets()
{
    // Java personnalisé forcé par l'utilisateur : on saute le provisionnement.
    if (m_forceJava && !m_javaPath.isEmpty()) {
        emit progress("Utilisation du java personnalisé : " + m_javaPath);
        m_resolvedJavaPath = m_javaPath;
        afterJavaReady();
        return;
    }
    emit progress("Vérification de Java…");
    m_java->provision(m_version.javaComponent.isEmpty()
                          ? QStringLiteral("jre-legacy") : m_version.javaComponent);
}

void InstanceManager::onFabricResolved(const FabricProfile &profile)
{
    // Fusion Fabric par-dessus la version vanilla :
    // - Fabric impose sa mainClass (KnotClient) ;
    // - ses libraries s'ajoutent au classpath (placées avant les vanilla) ;
    // - ses arguments JVM s'ajoutent à ceux de vanilla.
    m_version.mainClass = profile.mainClass;
    m_version.libraries = profile.libraries + m_version.libraries;
    m_version.jvmArgs  += profile.jvmArgs;
    dedupeLibraries(m_version.libraries);   // évite les doublons de classpath

    emit progress(QString("Fabric fusionné (%1 lib(s), mainClass %2).")
                      .arg(m_version.libraries.size())
                      .arg(profile.mainClass));
    provisionJavaThenAssets();
}

void InstanceManager::fetchAssetIndex()
{
    if (m_version.assetIndexUrl.isEmpty()) {
        assemblePlan({});  // pas d'assets (versions très anciennes) : on continue
        return;
    }
    QNetworkRequest req{QUrl(m_version.assetIndexUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("Index des assets inaccessible : " + reply->errorString());
            return;
        }
        assemblePlan(reply->readAll());
    });
}

void InstanceManager::assemblePlan(const QByteArray &assetIndexJson)
{
    LaunchPlan plan;
    plan.server  = m_server;
    plan.version = m_version;
    plan.gameDir = instanceDir(m_server.id);

    const QString id = m_version.id;

    // 1) client.jar
    {
        DownloadTask t;
        t.url = m_version.clientUrl;
        t.dest = clientJar(id);
        t.expectedHash = m_version.clientSha1;   // SHA1
        t.size = m_version.clientSize;
        plan.downloads << t;
    }

    // 2) libraries
    for (const Library &lib : m_version.libraries) {
        // Libs sans URL = produites localement par les processors Forge/NeoForge :
        // elles existent déjà dans libraries/ (rien à télécharger), on les garde
        // néanmoins dans le classpath via m_version.libraries.
        if (lib.url.isEmpty())
            continue;
        DownloadTask t;
        t.url = lib.url;
        t.dest = QDir(librariesRoot()).filePath(lib.path);
        t.expectedHash = lib.sha1;               // SHA1
        t.size = lib.size;
        plan.downloads << t;
    }

    // 3) index des assets (le fichier lui-même) + objets
    if (!assetIndexJson.isEmpty()) {
        DownloadTask idx;
        idx.url = m_version.assetIndexUrl;
        idx.dest = QDir(assetsRoot()).filePath("indexes/" + m_version.assetIndexId + ".json");
        idx.expectedHash = m_version.assetIndexSha1;
        plan.downloads << idx;

        const QJsonObject objects =
            QJsonDocument::fromJson(assetIndexJson).object().value("objects").toObject();
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            const QString hash = o.value("hash").toString();
            if (hash.size() < 2)
                continue;
            const QString sub = hash.left(2);
            DownloadTask t;
            t.url = kResourcesBase + sub + "/" + hash;
            t.dest = QDir(assetsRoot()).filePath("objects/" + sub + "/" + hash);
            t.expectedHash = hash;               // SHA1
            t.size = static_cast<qint64>(o.value("size").toDouble(0));
            plan.downloads << t;
        }
    }

    // 4) assets du repo — mods, plugins, resourcepacks, shaders (sync non-destructif,
    //    SHA256, téléchargement authentifié). Chacun va dans son dossier d'instance.
    {
        SyncManager sync(instanceDir(m_server.id));
        const SyncPlan sp = sync.computePlan(m_server);
        plan.toDeleteMods = sp.toDelete;

        const QByteArray bearer = QByteArray("Bearer ") + m_token.toUtf8();
        for (const AssetRef &a : sp.toDownload) {
            DownloadTask t;
            // Source = banque mutualisée du repo ; destination = dossier de l'instance.
            t.url = m_gh->apiContentsUrl(assetBankPath(m_server, a.type, a.entry));
            t.dest = QDir(instanceDir(m_server.id)).filePath(assetLocalPath(a.type, a.entry));
            t.expectedHash = a.entry.sha256;
            t.algo = QCryptographicHash::Sha256;
            t.size = a.entry.size;
            if (!m_token.isEmpty()) {
                t.headers.append({QByteArray("Authorization"), bearer});
                t.headers.append({QByteArray("Accept"), QByteArray("application/vnd.github.raw+json")});
            }
            plan.downloads << t;
        }
    }

    // 5) commande de lancement
    LaunchPaths paths;
    paths.javaPath      = m_resolvedJavaPath.isEmpty() ? m_javaPath : m_resolvedJavaPath;
    paths.gameDir       = instanceDir(m_server.id);
    paths.assetsRoot    = assetsRoot();
    paths.librariesRoot = librariesRoot();
    paths.nativesDir    = nativesDir(id);
    paths.clientJar     = clientJar(id);
    plan.launchCommand  = LaunchBuilder::build(m_version, m_session, paths);

    plan.valid = !plan.launchCommand.isEmpty() && m_version.valid;
    emit progress(QString("Plan prêt : %1 fichier(s) à vérifier/télécharger.")
                      .arg(plan.downloads.size()));
    emit planReady(plan);
}

} // namespace lami
