#include "bridge/Bridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <memory>

#include "auth/MicrosoftAuth.h"
#include "auth/Secret.h"
#include "core/AppConfig.h"
#include "core/InstanceManager.h"
#include "core/ModArchive.h"
#include "core/ModScanner.h"
#include "core/Publisher.h"
#include "github/GitHubClient.h"
#include "github/Models.h"
#include "minecraft/Downloader.h"
#include "roles/Permissions.h"
#include "sync/SyncManager.h"

namespace lami {

static QJsonArray namesOf(const QVector<ModEntry> &list)
{
    QJsonArray a;
    for (const ModEntry &m : list) a.append(m.file);
    return a;
}

QJsonObject serverToUiJson(const ServerInfo &s)
{
    const QString loader = s.loaderVersion.isEmpty()
        ? s.loader
        : QStringLiteral("%1 (%2)").arg(s.loader, s.loaderVersion);

    return QJsonObject{
        {"id", s.id},
        {"name", s.name},
        {"ip", s.address},
        {"version", s.minecraftVersion},
        {"loader", loader.isEmpty() ? QStringLiteral("Vanilla") : loader},
        {"mods", namesOf(s.mods)},
        {"plugins", namesOf(s.plugins)},
        {"resourcePacks", namesOf(s.resourcePacks)},
        {"shaders", namesOf(s.shaders)},
        {"installed", false},
    };
}

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , m_gh(new GitHubClient(config::owner(), config::repo(), config::branch(), this))
    , m_net(new QNetworkAccessManager(this))
{
    // Repo privé sans token côté client : les lectures passent par le Worker
    // (GET /file, cache au bord). Un token local (dev) reste possible sinon.
    if (useWorker())
        m_gh->setWorkerUrl(config::workerUrl());
    else if (!config::token().isEmpty())
        m_gh->setToken(config::token());
}

void Bridge::handle(const QJsonObject &request)
{
    const int id = request.value("id").toInt();
    const QString method = request.value("method").toString();
    const QJsonObject params = request.value("params").toObject();

    if (method == "resolveServer") {
        resolveServer(id, params);
    } else if (method == "listServers") {
        listServers(id);
    } else if (method == "listMcVersions") {
        listMcVersions(id);
    } else if (method == "listLoaderVersions") {
        listLoaderVersions(id, params);
    } else if (method == "listInstalled") {
        listInstalled(id);
    } else if (method == "login") {
        login(id, params);
    } else if (method == "silentLogin") {
        silentLogin(id);
    } else if (method == "logout") {
        logout(id);
    } else if (method == "devLogin") {
        devLogin(id, params);
    } else if (method == "startDownload") {
        startDownload(id, params);
    } else if (method == "cancelDownload") {
        cancelDownload(id, params);
    } else if (method == "launch") {
        launch(id, params);
    } else if (method == "stopGame") {
        stopGame(id, params);
    } else if (method == "checkUpdate") {
        checkUpdate(id, params);
    } else if (method == "installUpdate") {
        installUpdate(id, params);
    } else if (method == "openUrl") {
        openUrl(id, params);
    } else if (method == "uninstall") {
        uninstall(id, params);
    } else if (method == "listBackgrounds") {
        listBackgrounds(id);
    } else if (method == "getSettings") {
        getSettings(id);
    } else if (method == "saveSettings") {
        saveSettings(id, params);
    } else if (method == "setToken") {
        setToken(id, params);
    } else if (method == "editServer") {
        editServer(id, params);
    } else if (method == "deleteServer") {
        deleteServer(id, params);
    } else if (method == "publishServer") {
        publishServer(id, params);
    } else if (method == "listRoles") {
        listRoles(id);
    } else if (method == "setRole") {
        setRole(id, params);
    } else if (method == "removeRole") {
        removeRole(id, params);
    } else {
        replyError(id, QStringLiteral("Méthode inconnue : %1").arg(method));
    }
}

void Bridge::listServers(int id)
{
    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns]() {
        for (const auto &c : *conns) QObject::disconnect(c);
        conns->clear();
    };

    *conns << connect(m_gh, &GitHubClient::serversFetched, this,
                      [this, id, cleanup](const QVector<ServerInfo> &servers) {
        cleanup();
        QJsonArray arr;
        for (const ServerInfo &s : servers)
            arr.append(serverToUiJson(s));
        replyOk(id, QJsonObject{{"servers", arr}});
    });
    *conns << connect(m_gh, &GitHubClient::errorOccurred, this,
                      [this, id, cleanup](const QString &e) {
        cleanup();
        replyError(id, e);
    });

    m_gh->fetchAllServers();
}

namespace {
QString settingsPath() { return config::settingsFile(); }   // emplacement fixe
int readRamGb() {
    return qBound(2, config::readSettings().value("ramGb").toInt(6), 32);
}
} // namespace

// Ferme le jeu en cours pour ce serveur.
void Bridge::stopGame(int id, const QJsonObject &params)
{
    const QString sid = params.value("id").toString();
    QProcess *p = m_running.value(sid, nullptr);
    if (!p) { replyError(id, "Aucun jeu en cours."); return; }
    p->kill();
    replyOk(id, QJsonObject{{"id", sid}, {"stopped", true}});
}

namespace {
// Compare deux versions "x.y.z" : renvoie true si `latest` > `current`.
bool isNewer(const QString &latest, const QString &current)
{
    const QStringList a = latest.split('.');
    const QStringList b = current.split('.');
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i].toInt() : 0;
        const int y = i < b.size() ? b[i].toInt() : 0;
        if (x != y) return x > y;
    }
    return false;
}
} // namespace

// Vérifie s'il existe une version plus récente (dernière Release GitHub).
void Bridge::checkUpdate(int id, const QJsonObject &params)
{
    const QString current = params.value("version").toString("0.0.0");

    QNetworkRequest req{QUrl("https://api.github.com/repos/Gimaxe/LAMI/releases/latest")};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, current]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Pas de release / hors-ligne → on ne bloque pas l'app.
            replyOk(id, QJsonObject{{"updateAvailable", false}});
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = o.value("tag_name").toString();
        if (tag.startsWith('v')) tag.remove(0, 1);
        const bool avail = !tag.isEmpty() && isNewer(tag, current);
        replyOk(id, QJsonObject{
            {"updateAvailable", avail},
            {"latest", tag},
            {"current", current},
            {"url", o.value("html_url").toString("https://github.com/Gimaxe/LAMI/releases/latest")},
        });
    });
}

// Auto-mise à jour : télécharge l'archive de la dernière release pour la
// plateforme courante, puis lance un script détaché qui ferme l'app, remplace
// les fichiers en place et relance le launcher. Aucun remplacement manuel.
void Bridge::installUpdate(int id, const QJsonObject &params)
{
    Q_UNUSED(params);
#if defined(Q_OS_WIN)
    const QString wantPlatform = "windows";
    const QString wantExt = ".zip";
#else
    const QString wantPlatform = "linux";
    const QString wantExt = ".tar.gz";
#endif

    QNetworkRequest req{QUrl("https://api.github.com/repos/Gimaxe/LAMI/releases/latest")};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, wantPlatform, wantExt]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            replyError(id, "Release introuvable : " + reply->errorString());
            return;
        }
        const QJsonObject rel = QJsonDocument::fromJson(reply->readAll()).object();
        // Choix de l'asset correspondant à la plateforme.
        QString assetUrl, assetName;
        for (const QJsonValue &av : rel.value("assets").toArray()) {
            const QJsonObject a = av.toObject();
            const QString n = a.value("name").toString();
            if (n.contains(wantPlatform) && n.endsWith(wantExt)) {
                assetName = n;
                assetUrl  = a.value("browser_download_url").toString();
                break;
            }
        }
        if (assetUrl.isEmpty()) {
            replyError(id, "Aucun paquet de mise à jour pour cette plateforme.");
            return;
        }

        emit event(QJsonObject{{"event", "updateProgress"}, {"step", "Téléchargement…"}, {"percent", 0}});

        const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QString archive = QDir(tmp).filePath(assetName);

        QNetworkRequest dreq{QUrl(assetUrl)};
        dreq.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
        dreq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *dl = m_net->get(dreq);
        connect(dl, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
            emit event(QJsonObject{{"event", "updateProgress"}, {"step", "Téléchargement…"},
                                   {"percent", total > 0 ? int(done * 100 / total) : 0}});
        });
        connect(dl, &QNetworkReply::finished, this, [this, id, dl, archive]() {
            dl->deleteLater();
            if (dl->error() != QNetworkReply::NoError) {
                replyError(id, "Téléchargement de la mise à jour échoué : " + dl->errorString());
                return;
            }
            QFile f(archive);
            if (!f.open(QIODevice::WriteOnly) || f.write(dl->readAll()) < 0) {
                replyError(id, "Écriture de l'archive impossible.");
                return;
            }
            f.close();

            const QString installDir = QCoreApplication::applicationDirPath();
            const QString staging = QDir(QStandardPaths::writableLocation(
                QStandardPaths::TempLocation)).filePath("lami-update-staging");

            if (!writeAndRunUpdater(installDir, archive, staging)) {
                replyError(id, "Impossible de lancer le programme de mise à jour.");
                return;
            }

            emit event(QJsonObject{{"event", "updateProgress"},
                                   {"step", "Installation… l'application va redémarrer."},
                                   {"percent", 100}});
            replyOk(id, QJsonObject{{"installing", true}});

            // On laisse le temps à l'événement de partir, puis on quitte pour
            // libérer les fichiers (le script force la fermeture de toute façon).
            QTimer::singleShot(1200, qApp, &QCoreApplication::quit);
        });
    });
}

// Écrit le script de mise à jour propre à l'OS et le lance détaché.
bool Bridge::writeAndRunUpdater(const QString &installDir, const QString &archive,
                                const QString &staging)
{
    const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
#if defined(Q_OS_WIN)
    const QString script = QDir(tmp).filePath("lami-update.ps1");
    QString ps;
    ps += "Start-Sleep -Seconds 1\n";
    ps += "Get-Process lami_shell,lami_backend -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue\n";
    ps += "Start-Sleep -Seconds 1\n";
    ps += "$staging = '" + QDir::toNativeSeparators(staging) + "'\n";
    ps += "Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue\n";
    ps += "New-Item -ItemType Directory -Force -Path $staging | Out-Null\n";
    ps += "Expand-Archive -Path '" + QDir::toNativeSeparators(archive) + "' -DestinationPath $staging -Force\n";
    // Quelques tentatives au cas où un fichier serait encore verrouillé.
    ps += "for ($i=0; $i -lt 10; $i++) {\n";
    ps += "  try { Copy-Item -Path (Join-Path $staging '*') -Destination '" + QDir::toNativeSeparators(installDir) + "' -Recurse -Force -ErrorAction Stop; break }\n";
    ps += "  catch { Start-Sleep -Seconds 1 }\n";
    ps += "}\n";
    ps += "Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue\n";
    ps += "Remove-Item -Force '" + QDir::toNativeSeparators(archive) + "' -ErrorAction SilentlyContinue\n";
    ps += "Start-Process -FilePath '" + QDir::toNativeSeparators(installDir) + "\\lami_shell.exe'\n";

    QFile f(script);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(ps.toUtf8()); f.close();
    return QProcess::startDetached("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass",
                                                  "-WindowStyle", "Hidden", "-File", script});
#else
    const QString script = QDir(tmp).filePath("lami-update.sh");
    QString sh;
    sh += "#!/bin/sh\n";
    sh += "sleep 1\n";
    sh += "pkill -f '/lami_shell' 2>/dev/null\n";
    sh += "pkill -f '/lami_backend' 2>/dev/null\n";
    sh += "sleep 1\n";
    sh += "staging='" + staging + "'\n";
    sh += "rm -rf \"$staging\"; mkdir -p \"$staging\"\n";
    sh += "tar xzf '" + archive + "' -C \"$staging\"\n";
    sh += "cp -a \"$staging/.\" '" + installDir + "/'\n";
    sh += "rm -rf \"$staging\" '" + archive + "'\n";
    sh += "chmod +x '" + installDir + "/lami_shell' '" + installDir + "/lami_backend' 2>/dev/null\n";
    sh += "( '" + installDir + "/lami_shell' >/dev/null 2>&1 & )\n";

    QFile f(script);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(sh.toUtf8()); f.close();
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner);
    return QProcess::startDetached("/bin/sh", {script});
#endif
}

// Ouvre une URL dans le navigateur par défaut de l'OS.
void Bridge::openUrl(int id, const QJsonObject &params)
{
    const QString url = params.value("url").toString();
    if (url.isEmpty()) { replyError(id, "URL manquante."); return; }
#if defined(Q_OS_WIN)
    QProcess::startDetached("cmd", {"/c", "start", "", url});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {url});
#else
    QProcess::startDetached("xdg-open", {url});
#endif
    replyOk(id, QJsonObject{{"opened", true}});
}

// Désinstalle un serveur : supprime son instance locale.
void Bridge::uninstall(int id, const QJsonObject &params)
{
    const QString sid = params.value("id").toString();
    if (sid.isEmpty()) { replyError(id, "Identifiant manquant."); return; }
    QDir dir(QDir(config::dataRoot()).filePath("instances/" + sid));
    if (dir.exists())
        dir.removeRecursively();
    replyOk(id, QJsonObject{{"id", sid}});
}

void Bridge::getSettings(int id)
{
    const QJsonObject s = config::readSettings();
    replyOk(id, QJsonObject{
        {"ramGb", readRamGb()},
        {"dataRoot", s.value("dataRoot").toString()},
        {"defaultDataRoot", config::defaultDataRoot()},
        {"javaPath", s.value("javaPath").toString()},
        {"jvmArgs", s.value("jvmArgs").toString()},
        {"closeBehavior", s.value("closeBehavior").toString()},
        {"accentColor", s.value("accentColor").toString()},
        {"bgImage", s.value("bgImage").toString()},
        {"hasToken", !config::token().isEmpty()},
        {"workerUrl", s.value("workerUrl").toString()},
        {"hasSavedLogin", !secret::load("msrefresh").isEmpty()},
    });
}

// Enregistre le jeton GitHub dans le dossier de données (persiste entre
// réinstallations). Nécessaire pour lire/écrire le repo-BDD privé.
void Bridge::setToken(int id, const QJsonObject &params)
{
    const QString tok = params.value("token").toString().trimmed();
    QDir().mkpath(config::defaultDataRoot());
    QFile f(config::tokenFile());
    if (tok.isEmpty()) {
        f.remove();   // vider = supprimer le jeton
        m_gh->setToken(QString());
        replyOk(id, QJsonObject{{"hasToken", false}});
        return;
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        replyError(id, "Écriture du jeton impossible.");
        return;
    }
    f.write(tok.toUtf8());
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);  // 600
    // Applique immédiatement au client GitHub persistant.
    m_gh->setToken(tok);
    replyOk(id, QJsonObject{{"hasToken", true}});
}

// Liste les images de fond disponibles (web/assets/backgrounds à côté de l'exe).
void Bridge::listBackgrounds(int id)
{
    const QString dir = QCoreApplication::applicationDirPath() + "/web/assets/backgrounds";
    QJsonArray out;
    const QStringList filters{"*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif"};
    for (const QString &f : QDir(dir).entryList(filters, QDir::Files, QDir::Name))
        out.append(f);
    replyOk(id, QJsonObject{{"backgrounds", out}});
}

void Bridge::saveSettings(int id, const QJsonObject &params)
{
    // Fusion : on repart des réglages existants et on met à jour les champs fournis.
    QJsonObject s = config::readSettings();
    if (params.contains("ramGb"))
        s["ramGb"] = qBound(2, params.value("ramGb").toInt(6), 32);
    if (params.contains("dataRoot"))
        s["dataRoot"] = params.value("dataRoot").toString().trimmed();
    if (params.contains("javaPath"))
        s["javaPath"] = params.value("javaPath").toString().trimmed();
    if (params.contains("jvmArgs"))
        s["jvmArgs"] = params.value("jvmArgs").toString();
    if (params.contains("closeBehavior"))
        s["closeBehavior"] = params.value("closeBehavior").toString();
    if (params.contains("accentColor"))
        s["accentColor"] = params.value("accentColor").toString().trimmed();
    if (params.contains("bgImage"))
        s["bgImage"] = params.value("bgImage").toString();
    if (params.contains("workerUrl"))
        s["workerUrl"] = params.value("workerUrl").toString().trimmed();

    QDir().mkpath(config::defaultDataRoot());
    QFile f(settingsPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        replyError(id, "Écriture des réglages impossible.");
        return;
    }
    f.write(QJsonDocument(s).toJson());
    replyOk(id, s);
}

// Liste les versions Minecraft (releases) depuis le manifeste Mojang.
void Bridge::listMcVersions(int id)
{
    QNetworkRequest req{QUrl("https://launchermeta.mojang.com/mc/game/version_manifest_v2.json")};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { replyError(id, "Versions Minecraft inaccessibles."); return; }
        QJsonArray out;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).object().value("versions").toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value("type").toString() == "release")
                out.append(o.value("id").toString());
        }
        replyOk(id, QJsonObject{{"versions", out}});
    });
}

// Liste les versions du loader (Fabric/Quilt via leur meta ; sinon vide → manuel).
void Bridge::listLoaderVersions(int id, const QJsonObject &params)
{
    const QString loader = params.value("loader").toString().trimmed().toLower();
    const QString mc = params.value("mcVersion").toString().trimmed();

    QString url;
    bool xml = false;   // Forge/NeoForge = maven-metadata.xml ; Fabric/Quilt = JSON
    // Fabric/Quilt : l'endpoint « /loader/<mcVersion> » ne renvoie QUE les loaders
    // compatibles avec cette version de Minecraft (sinon la liste complète).
    if (loader == "fabric")
        url = "https://meta.fabricmc.net/v2/versions/loader" + (mc.isEmpty() ? QString() : "/" + mc);
    else if (loader == "quilt")
        url = "https://meta.quiltmc.org/v3/versions/loader" + (mc.isEmpty() ? QString() : "/" + mc);
    else if (loader == "forge")  { url = "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml"; xml = true; }
    else if (loader == "neoforge"){ url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml"; xml = true; }
    else { replyOk(id, QJsonObject{{"versions", QJsonArray{}}}); return; }

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, loader, mc, xml]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { replyOk(id, QJsonObject{{"versions", QJsonArray{}}}); return; }
        const QByteArray body = reply->readAll();
        QJsonArray out;

        if (!xml) {
            // Fabric / Quilt : JSON, versions déjà triées (dernière en premier).
            // Endpoint générique : objets { "version": ... }.
            // Endpoint par version de jeu : objets { "loader": { "version": ... } }.
            for (const QJsonValue &v : QJsonDocument::fromJson(body).array()) {
                const QJsonObject o = v.toObject();
                QString ver = o.value("loader").toObject().value("version").toString();
                if (ver.isEmpty()) ver = o.value("version").toString();
                if (!ver.isEmpty()) out.append(ver);
            }
            replyOk(id, QJsonObject{{"versions", out}});
            return;
        }

        // Forge / NeoForge : maven-metadata.xml, versions triées croissant.
        const QString text = QString::fromUtf8(body);
        QRegularExpression re("<version>([^<]+)</version>");
        QStringList all;
        auto it = re.globalMatch(text);
        while (it.hasNext()) all << it.next().captured(1);

        // Préfixe de filtrage selon la version Minecraft choisie.
        QString prefix;      // Forge : "1.20.1-"   NeoForge : "21.1."
        if (loader == "forge" && !mc.isEmpty()) {
            prefix = mc + "-";
        } else if (loader == "neoforge" && !mc.isEmpty()) {
            const QStringList p = mc.split('.');
            if (p.size() >= 2)
                prefix = p.value(1) + "." + (p.size() >= 3 ? p.value(2) : QStringLiteral("0")) + ".";
        }

        QStringList kept;
        for (const QString &v : all) {
            if (!prefix.isEmpty() && !v.startsWith(prefix)) continue;
            // Forge : on retire le préfixe "<mc>-" pour n'afficher que la version du loader.
            kept << (loader == "forge" && !prefix.isEmpty() ? QString(v).mid(prefix.size()) : v);
        }
        // Filtre strict : on ne montre QUE les versions compatibles avec la version
        // Minecraft choisie (ex. NeoForge n'existe pas pour 1.20.1 → liste vide,
        // c'est volontaire : ce loader n'est pas compatible avec cette version).

        // Dernière version en premier (le maven-metadata est croissant).
        std::reverse(kept.begin(), kept.end());
        for (const QString &v : kept) out.append(v);
        replyOk(id, QJsonObject{{"versions", out}});
    });
}

// Liste les serveurs réellement INSTALLÉS localement (dossier instances/<id>).
void Bridge::listInstalled(int id)
{
    QDir dir(config::dataRoot() + "/instances");
    QJsonArray arr;
    for (const QString &d : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        arr.append(d);
    replyOk(id, QJsonObject{{"installed", arr}});
}

// Résout le rôle (via le Worker si configuré, sinon roles.json + repli owner)
// puis répond à l'UI avec uuid/name/token/role.
void Bridge::resolveRoleAndReply(int id, const MinecraftSession &s)
{
    // Owner = super admin, quoi qu'il arrive (repli ultime).
    const bool owner = QString(s.uuid).remove('-').toLower() == "6ce55042b80845c4999b54c99cd96398";

    const QString wurl = config::workerUrl();
    if (!wurl.isEmpty()) {
        QNetworkRequest req{QUrl(wurl + "/whoami")};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
        QNetworkReply *reply = m_net->post(
            req, QJsonDocument(QJsonObject{{"token", s.minecraftToken}}).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, id, s, reply, owner]() {
            reply->deleteLater();
            const int httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();

            // 401 = le Worker a rejeté le token Minecraft (invalide/expiré) : c'est
            // une identité NON authentifiée, pas juste un rôle inconnu. Le masquer
            // derrière un repli "superadmin" tromperait l'utilisateur (il croirait
            // être connecté alors que toute écriture échouera ensuite). On invalide
            // donc la session et on force une reconnexion.
            if (httpStatus == 401) {
                m_session = MinecraftSession{};
                replyError(id, o.value("error").toString(
                                   "Session Microsoft expirée, reconnecte-toi."));
                return;
            }

            QString role, diag;
            if (reply->error() != QNetworkReply::NoError)
                diag = "Worker injoignable : " + reply->errorString();
            else if (o.contains("error"))
                diag = "Worker : " + o.value("error").toString();
            else
                role = o.value("role").toString();
            if (role.isEmpty()) {
                // Repli UNIQUEMENT pour un Worker injoignable/en erreur (pas pour un
                // token rejeté, traité ci-dessus) : ne jamais fabriquer un faux rôle.
                role = owner ? "superadmin" : "player";
                if (!diag.isEmpty())
                    emit event(QJsonObject{{"event", "loginProgress"}, {"step", diag}});
            }
            replyOk(id, QJsonObject{{"uuid", s.uuid}, {"name", s.name},
                                    {"token", s.minecraftToken}, {"role", role}});
        });
        return;
    }

    // Sans Worker : lecture directe de roles.json (token requis), sinon repli owner.
    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns]() { for (const auto &c : *conns) QObject::disconnect(c); conns->clear(); };
    *conns << connect(m_gh, &GitHubClient::rolesFetched, this,
                      [this, id, s, cleanup](const RoleTable &roles) {
        cleanup();
        replyOk(id, QJsonObject{{"uuid", s.uuid}, {"name", s.name},
                                {"token", s.minecraftToken},
                                {"role", roleToString(RoleResolver::roleFor(s.uuid, roles))}});
    });
    *conns << connect(m_gh, &GitHubClient::errorOccurred, this,
                      [this, id, s, cleanup, owner](const QString &) {
        cleanup();
        replyOk(id, QJsonObject{{"uuid", s.uuid}, {"name", s.name},
                                {"token", s.minecraftToken}, {"role", owner ? "superadmin" : "player"}});
    });
    m_gh->fetchRoles();
}

void Bridge::login(int id, const QJsonObject &params)
{
    const QString clientId = config::clientId();
    if (clientId.isEmpty()) { replyError(id, "client_id Azure manquant."); return; }
    if (m_auth) { m_auth->deleteLater(); m_auth = nullptr; }

    const bool remember = params.value("remember").toBool();
    m_auth = new MicrosoftAuth(clientId, this);

    connect(m_auth, &MicrosoftAuth::deviceCodeReady, this,
            [this](const QString &code, const QString &uri) {
        emit event(QJsonObject{{"event", "loginCode"}, {"code", code}, {"uri", uri}});
    });
    connect(m_auth, &MicrosoftAuth::progress, this, [this](const QString &s) {
        emit event(QJsonObject{{"event", "loginProgress"}, {"step", s}});
    });
    connect(m_auth, &MicrosoftAuth::authenticated, this,
            [this, id, remember](const MinecraftSession &s) {
        m_session = s;
        m_auth->deleteLater();
        m_auth = nullptr;
        // « Se souvenir de moi » : refresh token chiffré par l'OS (DPAPI/libsecret).
        if (remember && !s.msRefreshToken.isEmpty())
            secret::save("msrefresh", s.msRefreshToken);
        resolveRoleAndReply(id, s);
    });
    connect(m_auth, &MicrosoftAuth::failed, this, [this, id](const QString &msg) {
        replyError(id, msg);
        m_auth->deleteLater();
        m_auth = nullptr;
    });
    m_auth->start();
}

// Reconnexion silencieuse au démarrage via le refresh token sauvegardé.
void Bridge::silentLogin(int id)
{
    const QString refresh = secret::load("msrefresh");
    if (refresh.isEmpty()) { replyError(id, "Aucune session mémorisée."); return; }
    const QString clientId = config::clientId();
    if (clientId.isEmpty()) { replyError(id, "client_id Azure manquant."); return; }
    if (m_auth) { m_auth->deleteLater(); m_auth = nullptr; }

    m_auth = new MicrosoftAuth(clientId, this);
    connect(m_auth, &MicrosoftAuth::authenticated, this, [this, id](const MinecraftSession &s) {
        m_session = s;
        m_auth->deleteLater();
        m_auth = nullptr;
        // Le refresh token tourne : on réécrit le nouveau (chiffré).
        if (!s.msRefreshToken.isEmpty())
            secret::save("msrefresh", s.msRefreshToken);
        resolveRoleAndReply(id, s);
    });
    connect(m_auth, &MicrosoftAuth::failed, this, [this, id](const QString &msg) {
        // Session expirée : on efface le refresh pour repasser au login normal.
        secret::clear("msrefresh");
        replyError(id, msg);
        m_auth->deleteLater();
        m_auth = nullptr;
    });
    m_auth->startSilent(refresh);
}

void Bridge::logout(int id)
{
    secret::clear("msrefresh");   // oublie la session mémorisée
    m_session = MinecraftSession{};
    replyOk(id, QJsonObject{{"loggedOut", true}});
}

void Bridge::resolveServer(int id, const QJsonObject &params)
{
    const QString ip = params.value("ip").toString().trimmed();
    if (ip.isEmpty()) {
        replyError(id, "Adresse vide.");
        return;
    }

    // Connexions à usage unique : la première des deux qui se déclenche gagne,
    // puis on déconnecte les deux pour ne pas fuiter.
    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns]() {
        for (const auto &c : *conns) QObject::disconnect(c);
        conns->clear();
    };

    *conns << connect(m_gh, &GitHubClient::serverFetched, this,
                      [this, id, cleanup](const ServerInfo &s) {
        cleanup();
        replyOk(id, serverToUiJson(s));
    });
    *conns << connect(m_gh, &GitHubClient::errorOccurred, this,
                      [this, id, cleanup](const QString &e) {
        cleanup();
        replyError(id, e);
    });

    m_gh->fetchServerByAddress(ip);
}

// PROVISOIRE : établit une session côté backend SANS Microsoft (avant approbation
// Azure), avec l'identité gimaxe (super admin dans roles.json) pour tester les
// actions à privilèges (publier, admin). À RETIRER une fois le vrai login actif.
void Bridge::devLogin(int id, const QJsonObject &params)
{
    m_session.uuid = params.value("uuid").toString("6ce55042-b808-45c4-999b-54c99cd96398");
    m_session.name = params.value("name").toString("gimaxe");
    m_session.minecraftToken = "0";
    m_session.valid = true;

    // Résout le rôle réel depuis roles.json pour le renvoyer à l'UI.
    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns]() { for (const auto &c : *conns) QObject::disconnect(c); conns->clear(); };
    *conns << connect(m_gh, &GitHubClient::rolesFetched, this,
                      [this, id, cleanup](const RoleTable &roles) {
        cleanup();
        const Role r = RoleResolver::roleFor(m_session.uuid, roles);
        replyOk(id, QJsonObject{{"uuid", m_session.uuid}, {"name", m_session.name},
                                {"role", roleToString(r)}});
    });
    *conns << connect(m_gh, &GitHubClient::errorOccurred, this, [this, id, cleanup](const QString &) {
        cleanup();
        // roles.json illisible (repo privé sans token) : la connexion DEV de
        // l'owner « gimaxe » reste super admin pour pouvoir tester.
        const bool owner = QString(m_session.uuid).remove('-').toLower() == "6ce55042b80845c4999b54c99cd96398";
        replyOk(id, QJsonObject{{"uuid", m_session.uuid}, {"name", m_session.name},
                                {"role", owner ? "superadmin" : "player"}});
    });
    m_gh->fetchRoles();
}

void Bridge::startDownload(int id, const QJsonObject &params)
{
    const QString serverId = params.value("id").toString();
    if (serverId.isEmpty()) {
        replyError(id, "Identifiant de serveur manquant.");
        return;
    }
    // Jamais deux téléchargements du même serveur en parallèle : sinon
    // « Annuler » ne tue que le plus récent et l'autre continue en fond.
    if (m_downloads.contains(serverId)) {
        replyError(id, "Un téléchargement de ce serveur est déjà en cours.");
        return;
    }

    auto *mgr = new InstanceManager(config::owner(), config::repo(), config::branch(),
                                    config::token(), config::dataRoot(), config::javaPath(), this);
    mgr->setForceJava(config::forceCustomJava());
    if (useWorker()) mgr->setWorkerUrl(config::workerUrl());
    m_downloads.insert(serverId, ActiveDownload{mgr, nullptr, id});

    // Session neutre : le téléchargement des fichiers ne dépend pas de l'identité
    // (seul le LANCEMENT a besoin du vrai token).
    MinecraftSession session;
    session.name = "player"; session.uuid = "00000000000000000000000000000000"; session.valid = true;

    connect(mgr, &InstanceManager::progress, this, [this, serverId](const QString &s) {
        emit event(QJsonObject{{"event", "downloadStatus"}, {"id", serverId}, {"step", s}});
    });
    connect(mgr, &InstanceManager::failed, this, [this, id, serverId, mgr](const QString &e) {
        m_downloads.remove(serverId);
        replyError(id, e);
        mgr->deleteLater();
    });
    connect(mgr, &InstanceManager::planReady, this,
            [this, id, serverId, mgr](const LaunchPlan &plan) {
        auto *dl = new Downloader(6, this);
        m_downloads.insert(serverId, ActiveDownload{mgr, dl, id});

        connect(dl, &Downloader::progressBytes, this, [this, serverId](qint64 done, qint64 total) {
            emit event(QJsonObject{{"event", "downloadProgress"}, {"id", serverId},
                                   {"doneMb", done / 1048576.0}, {"totalMb", total / 1048576.0},
                                   {"percent", total > 0 ? int(done * 100 / total) : 0}});
        });
        // Chaque échec de fichier est remonté à l'UI avec sa raison (diagnostic).
        connect(dl, &Downloader::fileFailed, this, [this, serverId](const QString &dest, const QString &reason) {
            emit event(QJsonObject{{"event", "downloadFileFailed"}, {"id", serverId},
                                   {"file", QFileInfo(dest).fileName()}, {"reason", reason}});
        });
        // Annulation : on répond à la requête d'origine et on nettoie (les fichiers
        // déjà téléchargés restent — hashés, ils seront repris au prochain essai).
        connect(dl, &Downloader::aborted, this, [this, id, serverId, mgr, dl]() {
            m_downloads.remove(serverId);
            replyOk(id, QJsonObject{{"id", serverId}, {"cancelled", true}});
            dl->deleteLater();
            mgr->deleteLater();
        });
        connect(dl, &Downloader::finished, this,
                [this, id, serverId, plan, mgr, dl](int ok, int failed) {
            m_downloads.remove(serverId);
            // Marque les mods du repo comme gérés par le launcher (sync non-destructif).
            SyncManager sync(plan.gameDir);
            QStringList assetPaths;
            for (const char *type : {assets::Mods, assets::Plugins, assets::ResourcePacks, assets::Shaders})
                for (const ModEntry &m : plan.server.assetList(type))
                    assetPaths << assetLocalPath(type, m);
            if (!assetPaths.isEmpty())
                sync.markInstalled(assetPaths);

            replyOk(id, QJsonObject{{"id", serverId}, {"downloaded", ok}, {"failed", failed}});
            emit event(QJsonObject{{"event", "downloadDone"}, {"id", serverId}, {"failed", failed}});
            dl->deleteLater();
            mgr->deleteLater();
        });

        dl->start(plan.downloads);
    });

    mgr->plan(serverId, session);
}

// VRAIE annulation : coupe les transferts en cours (Downloader::abort) ou stoppe
// la planification (destruction du manager). Sans elle, cliquer « Annuler » ne
// faisait que masquer la barre côté UI pendant que tout continuait en fond.
void Bridge::cancelDownload(int id, const QJsonObject &params)
{
    const QString serverId = params.value("id").toString();
    const auto it = m_downloads.constFind(serverId);
    if (it == m_downloads.constEnd()) {
        replyOk(id, QJsonObject{{"id", serverId}, {"cancelled", false}});
        return;
    }
    const ActiveDownload ad = it.value();
    if (auto *dl = qobject_cast<Downloader *>(ad.dl.data())) {
        dl->abort();   // → signal aborted() : répond à la requête startDownload et nettoie
    } else if (!ad.mgr.isNull()) {
        // Encore en phase de planification : détruire le manager coupe ses
        // requêtes (parentées) et empêche planReady de démarrer les transferts.
        // On répond aussi à la requête startDownload d'origine (sinon timeout JS).
        m_downloads.remove(serverId);
        ad.mgr->deleteLater();
        if (ad.requestId >= 0)
            replyOk(ad.requestId, QJsonObject{{"id", serverId}, {"cancelled", true}});
    }
    replyOk(id, QJsonObject{{"id", serverId}, {"cancelled", true}});
    emit event(QJsonObject{{"event", "downloadCancelled"}, {"id", serverId}});
}

namespace {
// --- NBT minimal (non compressé) pour écrire servers.dat -------------------
void nbtU16(QByteArray &b, quint16 v) { b.append(char(v >> 8)); b.append(char(v & 0xFF)); }
void nbtStr(QByteArray &b, const QByteArray &s) { nbtU16(b, quint16(s.size())); b.append(s); }
void nbtNamedStr(QByteArray &b, const QByteArray &name, const QByteArray &value)
{
    b.append(char(8));   // TAG_String
    nbtStr(b, name);
    nbtStr(b, value);
}

// Réglages par défaut d'une instance, créés SEULEMENT s'ils n'existent pas
// encore (jamais d'écrasement des choix du joueur) :
//  - options.txt : jeu en français (lang:fr_fr) ;
//  - servers.dat : le serveur de l'instance pré-enregistré dans la liste
//    multijoueur (NBT non compressé : compound racine → liste "servers").
void writeDefaultGameFiles(const QString &gameDir, const QString &srvName,
                           const QString &srvAddress)
{
    const QString optionsPath = QDir(gameDir).filePath("options.txt");
    if (!QFileInfo::exists(optionsPath)) {
        QFile f(optionsPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write("lang:fr_fr\n");
    }

    const QString serversPath = QDir(gameDir).filePath("servers.dat");
    if (!QFileInfo::exists(serversPath) && !srvAddress.isEmpty()) {
        QByteArray nbt;
        nbt.append(char(10)); nbtStr(nbt, "");           // TAG_Compound racine ""
        nbt.append(char(9));  nbtStr(nbt, "servers");    // TAG_List "servers"
        nbt.append(char(10));                            // ...de TAG_Compound
        nbt.append(char(0)); nbt.append(char(0)); nbt.append(char(0)); nbt.append(char(1));  // count = 1
        nbtNamedStr(nbt, "ip", srvAddress.toUtf8());
        nbtNamedStr(nbt, "name",
                    (srvName.isEmpty() ? srvAddress : srvName).toUtf8());
        nbt.append(char(0));                             // fin du compound élément
        nbt.append(char(0));                             // fin de la racine
        QFile f(serversPath);
        if (f.open(QIODevice::WriteOnly))
            f.write(nbt);
    }
}
} // namespace

void Bridge::launch(int id, const QJsonObject &params)
{
    const QString serverId = params.value("id").toString();
    const QString username  = params.value("username").toString("Player");
    if (serverId.isEmpty()) { replyError(id, "Identifiant de serveur manquant."); return; }

    // RAM à allouer (Go) : depuis les params, sinon depuis les réglages.
    int ramGb = params.value("ramGb").toInt(0);
    if (ramGb <= 0) ramGb = readRamGb();
    ramGb = qBound(2, ramGb, 32);

    auto *mgr = new InstanceManager(config::owner(), config::repo(), config::branch(),
                                    config::token(), config::dataRoot(), config::javaPath(), this);
    mgr->setForceJava(config::forceCustomJava());
    if (useWorker()) mgr->setWorkerUrl(config::workerUrl());

    // Session : la vraie si connecté (Microsoft approuvé), sinon un profil
    // "hors-ligne" pour au moins démarrer le jeu (menu principal).
    MinecraftSession session;
    session.name  = m_session.valid ? m_session.name : username;
    session.uuid  = m_session.valid ? m_session.uuid : "6ce55042b80845c4999b54c99cd96398";
    session.minecraftToken = m_session.valid ? m_session.minecraftToken : "0";
    session.valid = true;

    connect(mgr, &InstanceManager::progress, this, [this, serverId](const QString &s) {
        emit event(QJsonObject{{"event", "launchStatus"}, {"id", serverId}, {"step", s}});
    });
    connect(mgr, &InstanceManager::failed, this, [this, id, mgr](const QString &e) {
        replyError(id, e); mgr->deleteLater();
    });
    connect(mgr, &InstanceManager::planReady, this,
            [this, id, serverId, mgr, ramGb](const LaunchPlan &plan) {
        auto *dl = new Downloader(6, this);
        connect(dl, &Downloader::progressBytes, this, [this, serverId](qint64 done, qint64 total) {
            emit event(QJsonObject{{"event", "launchProgress"}, {"id", serverId},
                                   {"doneMb", done / 1048576.0}, {"totalMb", total / 1048576.0},
                                   {"percent", total > 0 ? int(done * 100 / total) : 0}});
        });
        connect(dl, &Downloader::finished, this,
                [this, id, serverId, plan, mgr, dl, ramGb](int, int failed) {
            dl->deleteLater(); mgr->deleteLater();

            // Marque les assets installés (sync non-destructif, toutes catégories).
            SyncManager sync(plan.gameDir);
            QStringList assetPaths;
            for (const char *type : {assets::Mods, assets::Plugins, assets::ResourcePacks, assets::Shaders})
                for (const ModEntry &m : plan.server.assetList(type))
                    assetPaths << assetLocalPath(type, m);
            if (!assetPaths.isEmpty()) sync.markInstalled(assetPaths);

            // Prépare les dossiers puis démarre le jeu (QProcess détaché).
            QDir().mkpath(plan.gameDir);
            // Premier lancement : jeu en français + serveur pré-enregistré dans
            // la liste multijoueur (jamais écrasé si le joueur a déjà des réglages).
            writeDefaultGameFiles(plan.gameDir, plan.server.name, plan.server.address);
            QStringList cmd = plan.launchCommand;
            if (cmd.isEmpty()) { replyError(id, "Commande de lancement vide."); return; }
            const QString program = cmd.takeFirst();
            // Allocation mémoire (RAM des réglages) en tête des arguments JVM.
            cmd.prepend(QStringLiteral("-Xmx%1G").arg(ramGb));
            cmd.prepend(QStringLiteral("-Xms%1G").arg(qMax(1, ramGb / 2)));

            // Arguments JVM personnalisés (réglages) : insérés en tête, avant la
            // mainClass. On découpe la chaîne en respectant les guillemets.
            const QString extra = config::jvmArgs().trimmed();
            if (!extra.isEmpty()) {
                const QStringList parts = QProcess::splitCommand(extra);
                for (int i = parts.size() - 1; i >= 0; --i)
                    cmd.prepend(parts.at(i));
            }

            emit event(QJsonObject{{"event", "launchStatus"}, {"id", serverId},
                                   {"step", failed > 0
                                        ? QStringLiteral("Démarrage du jeu (%1 fichier(s) en échec)…").arg(failed)
                                        : QStringLiteral("Démarrage du jeu…")}});

            auto *proc = new QProcess(this);
            proc->setWorkingDirectory(plan.gameDir);
            // Logs du jeu : stdout et stderr fusionnés, poussés ligne par ligne
            // à l'UI (panneau « Logs » de la page serveur). Bornés côté UI.
            proc->setProcessChannelMode(QProcess::MergedChannels);
            connect(proc, &QProcess::readyReadStandardOutput, this, [this, serverId, proc]() {
                while (proc->canReadLine()) {
                    const QString line = QString::fromUtf8(proc->readLine()).trimmed();
                    if (!line.isEmpty())
                        emit event(QJsonObject{{"event", "gameLog"}, {"id", serverId}, {"line", line}});
                }
            });
            connect(proc, &QProcess::started, this, [this, id, serverId, proc]() {
                m_running.insert(serverId, proc);   // suivi pour pouvoir le fermer
                emit event(QJsonObject{{"event", "launched"}, {"id", serverId}});
                replyOk(id, QJsonObject{{"id", serverId}, {"launched", true}});
            });
            connect(proc, &QProcess::errorOccurred, this,
                    [this, id, proc](QProcess::ProcessError) {
                replyError(id, "Impossible de lancer Java : " + proc->errorString()
                               + " (Java 17 est-il installé ?)");
            });
            // À la fermeture du jeu → on prévient l'UI et on nettoie.
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, serverId, proc](int, QProcess::ExitStatus) {
                m_running.remove(serverId);
                emit event(QJsonObject{{"event", "gameClosed"}, {"id", serverId}});
                proc->deleteLater();
            });
            proc->start(program, cmd);
        });
        dl->start(plan.downloads);
    });
    mgr->plan(serverId, session);
}

namespace {
// Slug stable pour l'id du serveur (nom → minuscules, alphanumérique + tirets).
QString slugify(const QString &name)
{
    QString s;
    for (const QChar &c : name.toLower()) {
        if (c.isLetterOrNumber()) s += c;
        else if (!s.isEmpty() && s.back() != '-') s += '-';
    }
    while (s.endsWith('-')) s.chop(1);
    return s;
}
} // namespace

// --- Worker : routage des écritures -----------------------------------------
bool Bridge::useWorker() const { return !config::workerUrl().isEmpty(); }

// POST JSON vers le Worker ; ajoute automatiquement le token Minecraft (identité).
// La réponse du Worker est renvoyée telle quelle à l'UI (ou son "error").
void Bridge::postToWorker(int id, const QString &path, QJsonObject body)
{
    body.insert("token", m_session.minecraftToken);
    QNetworkRequest req{QUrl(config::workerUrl() + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
        reply->deleteLater();
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();

        // 401 = le Worker a rejeté le token Minecraft (invalide/expiré). La
        // session locale ment donc sur l'identité : on l'invalide et on prévient
        // l'UI (sessionExpired) pour qu'elle redemande une connexion, plutôt que
        // de laisser l'utilisateur croire qu'il est connecté alors que TOUTE
        // écriture continuerait d'échouer silencieusement en arrière-plan.
        if (httpStatus == 401) {
            m_session = MinecraftSession{};
            emit event(QJsonObject{{"event", "sessionExpired"}});
            replyError(id, o.value("error").toString(
                               "Session Microsoft expirée, reconnecte-toi."));
            return;
        }

        if (reply->error() != QNetworkReply::NoError || o.contains("error")) {
            replyError(id, o.value("error").toString(
                               "Worker : " + reply->errorString()));
            return;
        }
        replyOk(id, o);
    });
}

// Aplati les zips en une liste plate d'assets (avec base64) et remplit en même
// temps *assetListsOut = { type: [ {file, sha256, size} ] } (métadonnées seules,
// destinées au manifeste côté Worker après upload chunké).
QJsonArray Bridge::flattenAssetsForWorker(const QHash<QString, QString> &zips,
                                          QJsonObject *assetListsOut)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QJsonArray flat;
    for (auto it = zips.begin(); it != zips.end(); ++it) {
        const QString type = it.key();
        const QString tmp = QDir(base).filePath("lami-wrk-" + type);
        QDir(tmp).removeRecursively();
        QDir().mkpath(tmp);
        QString err;
        if (ModArchive::extract(it.value(), tmp, QString(), &err).isEmpty())
            continue;
        QJsonArray meta;
        for (const ModEntry &e : scanFolder(tmp)) {
            QFile f(QDir(tmp).filePath(e.file));
            if (!f.open(QIODevice::ReadOnly)) continue;
            flat.append(QJsonObject{
                {"type", type},
                {"file", e.file},
                {"base64", QString::fromLatin1(f.readAll().toBase64())},
                {"sha256", e.sha256},
                {"size", static_cast<double>(e.size)},
            });
            meta.append(QJsonObject{
                {"file", e.file}, {"sha256", e.sha256}, {"size", static_cast<double>(e.size)}});
        }
        if (assetListsOut && !meta.isEmpty()) assetListsOut->insert(type, meta);
    }
    return flat;
}

// Upload les assets par lots via POST /upload (bornés en taille ET en nombre pour
// rester sous les limites du Worker : requête ≤ ~100 Mo, ≤ 50 sous-requêtes), puis
// envoie la requête finale (publish/edit) avec seulement les métadonnées (assetLists).
void Bridge::publishChunked(int id, const QString &finalPath, QJsonObject finalBody,
                            const QString &mcVersion, const QString &loader,
                            const QHash<QString, QString> &zips)
{
    QJsonObject assetLists;
    auto flat = std::make_shared<QJsonArray>(flattenAssetsForWorker(zips, &assetLists));
    finalBody.insert("assetLists", assetLists);

    // Pas d'assets : rien à uploader, on publie directement.
    if (flat->isEmpty()) { postToWorker(id, finalPath, finalBody); return; }

    auto pos = std::make_shared<int>(0);
    auto fb  = std::make_shared<QJsonObject>(finalBody);
    auto step = std::make_shared<std::function<void()>>();
    const QString token = m_session.minecraftToken;

    *step = [this, id, finalPath, fb, flat, pos, step, mcVersion, loader, token]() {
        if (*pos >= flat->size()) {          // tout est uploadé → requête finale
            postToWorker(id, finalPath, *fb);
            return;
        }
        // Construit un lot : assets groupés par type, borné (≤ 40 Mo, ≤ 20 fichiers).
        QJsonObject byType;
        qint64 batchBytes = 0;
        int batchCount = 0;
        while (*pos < flat->size() && batchCount < 20 && batchBytes < 40 * 1024 * 1024) {
            const QJsonObject a = flat->at(*pos).toObject();
            const QString type = a.value("type").toString();
            QJsonArray arr = byType.value(type).toArray();
            arr.append(QJsonObject{{"file", a.value("file")}, {"base64", a.value("base64")},
                                   {"sha256", a.value("sha256")}, {"size", a.value("size")}});
            byType[type] = arr;
            batchBytes += a.value("base64").toString().size();
            batchCount++;
            (*pos)++;
        }

        QJsonObject body{{"token", token}, {"minecraft_version", mcVersion},
                         {"loader", loader}, {"assets", byType}};
        QNetworkRequest req{QUrl(config::workerUrl() + "/upload")};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
        QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, id, reply, step]() {
            reply->deleteLater();
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError || o.contains("error")) {
                replyError(id, o.value("error").toString("Upload Worker : " + reply->errorString()));
                return;                       // on stoppe la chaîne en cas d'échec
            }
            (*step)();                        // lot suivant
        });
    };
    (*step)();
}

void Bridge::publishServer(int id, const QJsonObject &params)
{
    // Action sensible : on exige une session Microsoft authentifiée, et on utilise
    // SON uuid (jamais un uuid fourni par le JS).
    if (!m_session.valid) {
        replyError(id, "Connecte-toi avec Microsoft avant de publier.");
        return;
    }

    ServerInfo srv;
    srv.name             = params.value("name").toString().trimmed();
    srv.address          = params.value("ip").toString().trimmed();
    srv.minecraftVersion = params.value("version").toString().trimmed();
    srv.loader           = params.value("loader").toString().trimmed().toLower();
    srv.loaderVersion    = params.value("loaderVersion").toString();
    srv.id               = params.value("id").toString().trimmed();
    if (srv.id.isEmpty())
        srv.id = slugify(srv.name);
    srv.owner            = m_session.uuid;   // créateur = session authentifiée (jamais le JS)
    srv.valid = true;

    if (srv.name.isEmpty() || srv.address.isEmpty() || srv.id.isEmpty()) {
        replyError(id, "Nom et adresse du serveur obligatoires.");
        return;
    }

    const QString sid = srv.id;

    // Les 4 zips d'assets (octets base64) fournis par le JS → fichiers temporaires.
    const QString tmpBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QHash<QString, QString> zips;
    const QHash<QString, QString> paramByType{
        {assets::Mods, "modsZip"}, {assets::Plugins, "pluginsZip"},
        {assets::ResourcePacks, "packsZip"}, {assets::Shaders, "shadersZip"}};
    for (auto it = paramByType.begin(); it != paramByType.end(); ++it) {
        const QString b64 = params.value(it.value()).toString();
        if (b64.isEmpty())
            continue;
        const QString tmpZip = QDir(tmpBase).filePath("lami-pub-" + sid + "-" + it.key() + ".zip");
        QFile f(tmpZip);
        if (!f.open(QIODevice::WriteOnly)) { replyError(id, "Écriture temporaire impossible."); return; }
        f.write(QByteArray::fromBase64(b64.toUtf8())); f.close();
        zips.insert(it.key(), tmpZip);
    }

    // Worker : il vérifie le rôle, fixe le propriétaire = UUID vérifié, uploade
    // les assets et écrit le manifeste. Aucun token GitHub côté client.
    if (useWorker()) {
        const QJsonObject server{
            {"id", srv.id}, {"name", srv.name}, {"address", srv.address},
            {"minecraft_version", srv.minecraftVersion}, {"loader", srv.loader},
            {"loader_version", srv.loaderVersion}};
        // Chunké : uploade les assets par lots via /upload, puis /publish (métadonnées).
        publishChunked(id, "/publish", QJsonObject{{"server", server}},
                       srv.minecraftVersion, srv.loader, zips);
        return;
    }

    // --- Chemin direct GitHub (sans Worker) : Publisher + token. ---
    auto *gh = new GitHubClient(config::owner(), config::repo(), config::branch(), this);
    if (!config::token().isEmpty())
        gh->setToken(config::token());
    auto *pub = new Publisher(gh, this);
    auto cleanup = [gh, pub]() { gh->deleteLater(); pub->deleteLater(); };

    connect(pub, &Publisher::progress, this, [this, sid](const QString &s) {
        emit event(QJsonObject{{"event", "publishProgress"}, {"id", sid}, {"step", s}});
    });
    connect(pub, &Publisher::published, this, [this, id, cleanup](const QString &serverId) {
        replyOk(id, QJsonObject{{"id", serverId}});
        cleanup();
    });
    connect(pub, &Publisher::denied, this, [this, id, cleanup](const QString &r) {
        replyError(id, r);
        cleanup();
    });
    connect(pub, &Publisher::failed, this, [this, id, cleanup](const QString &e) {
        replyError(id, e);
        cleanup();
    });

    if (zips.isEmpty()) {
        const QString tmpDir = QDir(tmpBase).filePath("lami-pub-empty-" + sid);
        QDir(tmpDir).removeRecursively();
        QDir().mkpath(tmpDir);
        pub->publishFromFolder(srv, tmpDir, m_session.uuid);
    } else {
        pub->publishFromZips(srv, zips, m_session.uuid);
    }
}

// Modifie un serveur : métadonnées (nom, adresse, version, loader, mot de passe)
// ET, si fourni, les assets — un zip par catégorie REMPLACE cette catégorie, un
// drapeau clear<Cat> la vide. Les catégories non touchées sont préservées.
void Bridge::editServer(int id, const QJsonObject &params)
{
    if (!m_session.valid) {
        replyError(id, "Connecte-toi avec Microsoft avant de modifier.");
        return;
    }
    const QString serverId = params.value("id").toString().trimmed();
    if (serverId.isEmpty()) { replyError(id, "Identifiant de serveur manquant."); return; }

    // Worker : vérifie la propriété, applique les changements + assets + vidages.
    if (useWorker()) {
        QJsonObject changes;
        auto sof = [&params](const char *k) { return params.value(k).toString().trimmed(); };
        if (!sof("name").isEmpty())    changes["name"] = sof("name");
        if (!sof("ip").isEmpty())      changes["address"] = sof("ip");
        if (!sof("version").isEmpty()) changes["minecraft_version"] = sof("version");
        if (!sof("loader").isEmpty())  changes["loader"] = sof("loader").toLower();
        if (params.contains("loaderVersion")) changes["loader_version"] = params.value("loaderVersion").toString();

        QJsonArray clearArr;
        const QHash<QString, QString> clr{
            {assets::Mods, "clearMods"}, {assets::Plugins, "clearPlugins"},
            {assets::ResourcePacks, "clearPacks"}, {assets::Shaders, "clearShaders"}};
        for (auto it = clr.begin(); it != clr.end(); ++it)
            if (params.value(it.value()).toBool()) clearArr.append(it.key());

        const QString tb = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QHash<QString, QString> zips;
        const QHash<QString, QString> zp{
            {assets::Mods, "modsZip"}, {assets::Plugins, "pluginsZip"},
            {assets::ResourcePacks, "packsZip"}, {assets::Shaders, "shadersZip"}};
        for (auto it = zp.begin(); it != zp.end(); ++it) {
            const QString b64 = params.value(it.value()).toString();
            if (b64.isEmpty()) continue;
            const QString z = QDir(tb).filePath("lami-edit-" + serverId + "-" + it.key() + ".zip");
            QFile f(z);
            if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray::fromBase64(b64.toUtf8())); f.close(); zips.insert(it.key(), z); }
        }
        // Version/loader cibles pour le chemin de banque des assets uploadés :
        // ceux du formulaire (toujours pré-remplis avec les valeurs du serveur).
        const QString mcVer = params.value("version").toString().trimmed();
        const QString ldr   = params.value("loader").toString().trimmed().toLower();
        publishChunked(id, "/edit",
                       QJsonObject{{"id", serverId}, {"changes", changes}, {"clear", clearArr}},
                       mcVer, ldr, zips);
        return;
    }

    auto *gh = new GitHubClient(config::owner(), config::repo(), config::branch(), this);
    if (!config::token().isEmpty())
        gh->setToken(config::token());

    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns, gh]() {
        for (const auto &c : *conns) QObject::disconnect(c);
        conns->clear();
        gh->deleteLater();
    };

    // 1) Lit le manifeste actuel (pour préserver les assets non touchés).
    *conns << connect(gh, &GitHubClient::serverFetched, this,
                      [this, id, gh, params, serverId, cleanup, conns](const ServerInfo &cur) {
        ServerInfo s = cur;   // conserve mods/plugins/resourcepacks/shaders
        auto strOf = [&params](const char *k) { return params.value(k).toString().trimmed(); };
        if (!strOf("name").isEmpty())    s.name = strOf("name");
        if (!strOf("ip").isEmpty())      s.address = strOf("ip");
        if (!strOf("version").isEmpty()) s.minecraftVersion = strOf("version");
        if (!strOf("loader").isEmpty())  s.loader = strOf("loader").toLower();
        if (params.contains("loaderVersion")) s.loaderVersion = params.value("loaderVersion").toString();

        // Catégories vidées explicitement par l'utilisateur.
        const QHash<QString, QString> clearFlag{
            {assets::Mods, "clearMods"}, {assets::Plugins, "clearPlugins"},
            {assets::ResourcePacks, "clearPacks"}, {assets::Shaders, "clearShaders"}};
        for (auto it = clearFlag.begin(); it != clearFlag.end(); ++it)
            if (params.value(it.value()).toBool())
                s.assetList(it.key()).clear();

        // Nouveaux zips fournis (base64) → écrits en temp, remplaceront la catégorie.
        const QString tmpBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QHash<QString, QString> zipParam{
            {assets::Mods, "modsZip"}, {assets::Plugins, "pluginsZip"},
            {assets::ResourcePacks, "packsZip"}, {assets::Shaders, "shadersZip"}};
        QHash<QString, QString> zips;
        for (auto it = zipParam.begin(); it != zipParam.end(); ++it) {
            const QString b64 = params.value(it.value()).toString();
            if (b64.isEmpty()) continue;
            const QString tmpZip = QDir(tmpBase).filePath("lami-edit-" + serverId + "-" + it.key() + ".zip");
            QFile f(tmpZip);
            if (!f.open(QIODevice::WriteOnly)) { cleanup(); replyError(id, "Écriture temporaire impossible."); return; }
            f.write(QByteArray::fromBase64(b64.toUtf8())); f.close();
            zips.insert(it.key(), tmpZip);
        }

        // Cas avec nouveaux fichiers → Publisher (upload + manifeste + index).
        if (!zips.isEmpty()) {
            // On retire nos handlers sur gh : Publisher pilote gh à partir d'ici
            // (évite un double-déclenchement sur writeError/errorOccurred).
            for (const auto &c : *conns) QObject::disconnect(c);
            conns->clear();
            auto *pub = new Publisher(gh, this);
            auto pcleanup = [gh, pub]() { pub->deleteLater(); gh->deleteLater(); };
            connect(pub, &Publisher::published, this, [this, id, serverId, pcleanup](const QString &) {
                pcleanup(); replyOk(id, QJsonObject{{"id", serverId}, {"edited", true}});
            });
            connect(pub, &Publisher::denied,  this, [this, id, pcleanup](const QString &e) { pcleanup(); replyError(id, e); });
            connect(pub, &Publisher::failed,  this, [this, id, pcleanup](const QString &e) { pcleanup(); replyError(id, e); });
            pub->publishFromZips(s, zips, m_session.uuid);
            return;
        }

        // Cas métadonnées / vidage seul → écriture directe du manifeste + index.
        *conns << connect(gh, &GitHubClient::filePut, this,
                          [this, id, gh, s, serverId, cleanup](const QString &) {
            if (s.address.isEmpty()) { cleanup(); replyOk(id, QJsonObject{{"id", serverId}, {"edited", true}}); return; }
            gh->upsertAddressIndex(s.address, s.id, "Maj adresse " + s.id + " via LAMI");
        });
        *conns << connect(gh, &GitHubClient::indexUpdated, this,
                          [this, id, serverId, cleanup]() {
            cleanup(); replyOk(id, QJsonObject{{"id", serverId}, {"edited", true}});
        });
        gh->putFile("servers/" + s.id + ".json",
                    QJsonDocument(serverToJson(s)).toJson(QJsonDocument::Indented),
                    "Modification du serveur " + s.id + " via LAMI");
    });
    *conns << connect(gh, &GitHubClient::writeError, this,
                      [this, id, cleanup](const QString &e) { cleanup(); replyError(id, e); });
    *conns << connect(gh, &GitHubClient::errorOccurred, this,
                      [this, id, cleanup](const QString &e) { cleanup(); replyError(id, e); });

    gh->fetchServer(serverId);
}

// Supprime complètement un serveur publié : son manifeste servers/<id>.json puis
// ses entrées dans servers/index.json. Nécessite une session authentifiée.
void Bridge::deleteServer(int id, const QJsonObject &params)
{
    if (!m_session.valid) {
        replyError(id, "Connecte-toi avec Microsoft avant de supprimer.");
        return;
    }
    const QString serverId = params.value("id").toString().trimmed();
    if (serverId.isEmpty()) { replyError(id, "Identifiant de serveur manquant."); return; }

    // Worker : il vérifie l'identité + la propriété avant de supprimer.
    if (useWorker()) { postToWorker(id, "/delete", QJsonObject{{"id", serverId}}); return; }

    auto *gh = new GitHubClient(config::owner(), config::repo(), config::branch(), this);
    if (!config::token().isEmpty())
        gh->setToken(config::token());

    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns, gh]() {
        for (const auto &c : *conns) QObject::disconnect(c);
        conns->clear();
        gh->deleteLater();
    };

    // 1) manifeste supprimé → 2) nettoyage de l'index → 3) réponse.
    *conns << connect(gh, &GitHubClient::fileDeleted, this, [gh](const QString &) {
        gh->removeFromIndex(gh->property("_delId").toString(),
                            "Suppression du serveur via LAMI");
    });
    *conns << connect(gh, &GitHubClient::indexUpdated, this,
                      [this, id, serverId, cleanup]() {
        cleanup();
        replyOk(id, QJsonObject{{"id", serverId}, {"deleted", true}});
    });
    *conns << connect(gh, &GitHubClient::writeError, this,
                      [this, id, cleanup](const QString &e) { cleanup(); replyError(id, e); });
    *conns << connect(gh, &GitHubClient::errorOccurred, this,
                      [this, id, cleanup](const QString &e) { cleanup(); replyError(id, e); });

    gh->setProperty("_delId", serverId);
    gh->deleteFile("servers/" + serverId + ".json", "Suppression du serveur " + serverId + " via LAMI");
}

void Bridge::requireSuperAdmin(int id, std::function<void(const RoleTable &)> action)
{
    if (!m_session.valid) {
        replyError(id, "Connecte-toi avec Microsoft.");
        return;
    }
    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns]() { for (const auto &c : *conns) QObject::disconnect(c); conns->clear(); };

    *conns << connect(m_gh, &GitHubClient::rolesFetched, this,
                      [this, id, cleanup, action](const RoleTable &roles) {
        cleanup();
        if (!RoleResolver::can(m_session.uuid, roles, Capability::ManageRoles)) {
            replyError(id, "Action réservée au Super Admin.");
            return;
        }
        action(roles);
    });
    *conns << connect(m_gh, &GitHubClient::errorOccurred, this,
                      [this, id, cleanup](const QString &e) { cleanup(); replyError(id, e); });

    m_gh->fetchRoles();
}

void Bridge::listRoles(int id)
{
    requireSuperAdmin(id, [this, id](const RoleTable &roles) {
        QJsonArray arr;
        for (auto it = roles.begin(); it != roles.end(); ++it)
            arr.append(QJsonObject{{"uuid", it.key()}, {"role", roleToString(it.value())}});
        replyOk(id, QJsonObject{{"roles", arr}});
    });
}

void Bridge::setRole(int id, const QJsonObject &params)
{
    const QString uuid = params.value("uuid").toString().trimmed();
    const QString role = params.value("role").toString("host");
    if (uuid.isEmpty()) { replyError(id, "UUID manquant."); return; }

    // Worker : il vérifie que l'appelant est super admin avant d'écrire roles.json.
    if (useWorker()) { postToWorker(id, "/setRole", QJsonObject{{"uuid", uuid}, {"role", role}}); return; }

    requireSuperAdmin(id, [this, id, uuid, role](const RoleTable &) {
        auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
        auto cleanup = [conns]() { for (const auto &c : *conns) QObject::disconnect(c); conns->clear(); };
        *conns << connect(m_gh, &GitHubClient::rolesUpdated, this, [this, id, uuid, role, cleanup]() {
            cleanup();
            replyOk(id, QJsonObject{{"uuid", uuid}, {"role", role}});
        });
        *conns << connect(m_gh, &GitHubClient::writeError, this, [this, id, cleanup](const QString &e) {
            cleanup(); replyError(id, e);
        });
        m_gh->setRole(uuid, role, QStringLiteral("Rôle %1 → %2 via LAMI").arg(uuid, role));
    });
}

void Bridge::removeRole(int id, const QJsonObject &params)
{
    const QString uuid = params.value("uuid").toString().trimmed();
    if (uuid.isEmpty()) { replyError(id, "UUID manquant."); return; }

    if (useWorker()) { postToWorker(id, "/removeRole", QJsonObject{{"uuid", uuid}}); return; }

    requireSuperAdmin(id, [this, id, uuid](const RoleTable &) {
        auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
        auto cleanup = [conns]() { for (const auto &c : *conns) QObject::disconnect(c); conns->clear(); };
        *conns << connect(m_gh, &GitHubClient::rolesUpdated, this, [this, id, uuid, cleanup]() {
            cleanup(); replyOk(id, QJsonObject{{"uuid", uuid}});
        });
        *conns << connect(m_gh, &GitHubClient::writeError, this, [this, id, cleanup](const QString &e) {
            cleanup(); replyError(id, e);
        });
        m_gh->removeRole(uuid, QStringLiteral("Révocation du rôle de %1 via LAMI").arg(uuid));
    });
}

void Bridge::replyOk(int id, const QJsonObject &result)
{
    emit response(QJsonObject{{"id", id}, {"ok", true}, {"result", result}});
}

void Bridge::replyError(int id, const QString &message)
{
    emit response(QJsonObject{{"id", id}, {"ok", false}, {"error", message}});
}

} // namespace lami
