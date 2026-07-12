#include "bridge/Bridge.h"

#include <QDir>
#include <QFile>
#include <QCryptographicHash>
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
#include "core/AppConfig.h"
#include "core/InstanceManager.h"
#include "core/Publisher.h"
#include "github/GitHubClient.h"
#include "github/Models.h"
#include "minecraft/Downloader.h"
#include "roles/Permissions.h"
#include "sync/SyncManager.h"

namespace lami {

// SHA-256 hex d'une chaîne (mot de passe).
static QString sha256Hex(const QString &s)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
}

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
        {"hasPassword", !s.passwordHash.isEmpty()},   // jamais le hash ni le mdp
        {"installed", false},
    };
}

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , m_gh(new GitHubClient(config::owner(), config::repo(), config::branch(), this))
    , m_net(new QNetworkAccessManager(this))
{
    if (!config::token().isEmpty())
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
        login(id);
    } else if (method == "devLogin") {
        devLogin(id, params);
    } else if (method == "checkPassword") {
        checkPassword(id, params);
    } else if (method == "startDownload") {
        startDownload(id, params);
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
    } else if (method == "getSettings") {
        getSettings(id);
    } else if (method == "saveSettings") {
        saveSettings(id, params);
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
QString settingsPath() { return QDir(config::dataRoot()).filePath("settings.json"); }
int readRamGb() {
    QFile f(settingsPath());
    int ram = 6;
    if (f.open(QIODevice::ReadOnly))
        ram = QJsonDocument::fromJson(f.readAll()).object().value("ramGb").toInt(6);
    return qBound(2, ram, 32);
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
    replyOk(id, QJsonObject{{"ramGb", readRamGb()}});
}

void Bridge::saveSettings(int id, const QJsonObject &params)
{
    const int ram = qBound(2, params.value("ramGb").toInt(6), 32);
    QDir().mkpath(config::dataRoot());
    QFile f(settingsPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        replyError(id, "Écriture des réglages impossible.");
        return;
    }
    f.write(QJsonDocument(QJsonObject{{"ramGb", ram}}).toJson());
    replyOk(id, QJsonObject{{"ramGb", ram}});
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

void Bridge::login(int id)
{
    const QString clientId = config::clientId();
    if (clientId.isEmpty()) {
        replyError(id, "client_id Azure manquant (.client_id).");
        return;
    }
    // Nouvelle tentative : on annule une éventuelle connexion en cours.
    if (m_auth) {
        m_auth->deleteLater();
        m_auth = nullptr;
    }

    m_auth = new MicrosoftAuth(clientId, this);

    // Le code à saisir est poussé comme événement (l'UI l'affiche).
    connect(m_auth, &MicrosoftAuth::deviceCodeReady, this,
            [this](const QString &code, const QString &uri) {
        emit event(QJsonObject{{"event", "loginCode"}, {"code", code}, {"uri", uri}});
    });
    connect(m_auth, &MicrosoftAuth::progress, this, [this](const QString &s) {
        emit event(QJsonObject{{"event", "loginProgress"}, {"step", s}});
    });
    connect(m_auth, &MicrosoftAuth::authenticated, this,
            [this, id](const MinecraftSession &s) {
        m_session = s;   // source de vérité pour les actions sensibles (publier…)
        replyOk(id, QJsonObject{
            {"uuid", s.uuid}, {"name", s.name}, {"token", s.minecraftToken}});
        m_auth->deleteLater();
        m_auth = nullptr;
    });
    connect(m_auth, &MicrosoftAuth::failed, this, [this, id](const QString &msg) {
        replyError(id, msg);
        m_auth->deleteLater();
        m_auth = nullptr;
    });

    m_auth->start();
}

// Vérifie un mot de passe SANS rien télécharger : récupère le manifeste du
// serveur et compare le hash. Permet à l'UI de garder la popup ouverte tant
// que le mot de passe est incorrect.
void Bridge::checkPassword(int id, const QJsonObject &params)
{
    const QString serverId = params.value("id").toString().trimmed();
    const QString pwd = params.value("password").toString();
    if (serverId.isEmpty()) { replyError(id, "Identifiant de serveur manquant."); return; }

    auto *gh = new GitHubClient(config::owner(), config::repo(), config::branch(), this);
    if (!config::token().isEmpty())
        gh->setToken(config::token());

    auto conns = std::make_shared<QVector<QMetaObject::Connection>>();
    auto cleanup = [conns, gh]() {
        for (const auto &c : *conns) QObject::disconnect(c);
        conns->clear();
        gh->deleteLater();
    };

    *conns << connect(gh, &GitHubClient::serverFetched, this,
                      [this, id, pwd, cleanup](const ServerInfo &s) {
        const bool ok = s.passwordHash.isEmpty()
                        || sha256Hex(pwd) == s.passwordHash;
        cleanup();
        replyOk(id, QJsonObject{{"ok", ok}});
    });
    *conns << connect(gh, &GitHubClient::errorOccurred, this,
                      [this, id, cleanup](const QString &e) {
        cleanup();
        replyError(id, e);
    });

    gh->fetchServer(serverId);
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
        replyOk(id, QJsonObject{{"uuid", m_session.uuid}, {"name", m_session.name},
                                {"role", "player"}});
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

    auto *mgr = new InstanceManager(config::owner(), config::repo(), config::branch(),
                                    config::token(), config::dataRoot(), config::javaPath(), this);
    mgr->setPassword(params.value("password").toString());

    // Session neutre : le téléchargement des fichiers ne dépend pas de l'identité
    // (seul le LANCEMENT a besoin du vrai token).
    MinecraftSession session;
    session.name = "player"; session.uuid = "00000000000000000000000000000000"; session.valid = true;

    connect(mgr, &InstanceManager::progress, this, [this, serverId](const QString &s) {
        emit event(QJsonObject{{"event", "downloadStatus"}, {"id", serverId}, {"step", s}});
    });
    connect(mgr, &InstanceManager::failed, this, [this, id, mgr](const QString &e) {
        replyError(id, e);
        mgr->deleteLater();
    });
    connect(mgr, &InstanceManager::planReady, this,
            [this, id, serverId, mgr](const LaunchPlan &plan) {
        auto *dl = new Downloader(6, this);

        connect(dl, &Downloader::progressBytes, this, [this, serverId](qint64 done, qint64 total) {
            emit event(QJsonObject{{"event", "downloadProgress"}, {"id", serverId},
                                   {"doneMb", done / 1048576.0}, {"totalMb", total / 1048576.0},
                                   {"percent", total > 0 ? int(done * 100 / total) : 0}});
        });
        connect(dl, &Downloader::finished, this,
                [this, id, serverId, plan, mgr, dl](int ok, int failed) {
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
    // Lancement d'un serveur déjà installé : pas de vérification de mot de passe
    // (il a été demandé une seule fois, au moment de l'installation).
    mgr->setVerifyPassword(false);

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
            QStringList cmd = plan.launchCommand;
            if (cmd.isEmpty()) { replyError(id, "Commande de lancement vide."); return; }
            const QString program = cmd.takeFirst();
            // Allocation mémoire (RAM des réglages) en tête des arguments JVM.
            cmd.prepend(QStringLiteral("-Xmx%1G").arg(ramGb));
            cmd.prepend(QStringLiteral("-Xms%1G").arg(qMax(1, ramGb / 2)));

            emit event(QJsonObject{{"event", "launchStatus"}, {"id", serverId},
                                   {"step", failed > 0
                                        ? QStringLiteral("Démarrage du jeu (%1 fichier(s) en échec)…").arg(failed)
                                        : QStringLiteral("Démarrage du jeu…")}});

            auto *proc = new QProcess(this);
            proc->setWorkingDirectory(plan.gameDir);
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
    // Mot de passe : on ne stocke QUE le hash (jamais le mot de passe en clair).
    const QString pwd = params.value("password").toString();
    if (!pwd.isEmpty())
        srv.passwordHash = sha256Hex(pwd);
    srv.valid = true;

    if (srv.name.isEmpty() || srv.address.isEmpty() || srv.id.isEmpty()) {
        replyError(id, "Nom et adresse du serveur obligatoires.");
        return;
    }

    auto *gh = new GitHubClient(config::owner(), config::repo(), config::branch(), this);
    if (!config::token().isEmpty())
        gh->setToken(config::token());
    auto *pub = new Publisher(gh, this);

    const QString sid = srv.id;
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

    // Les 4 zips d'assets (octets base64) fournis par le JS (webview : pas de chemin).
    // On écrit chacun dans un .zip temporaire ; les catégories vides sont ignorées.
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
        if (!f.open(QIODevice::WriteOnly)) { replyError(id, "Écriture temporaire impossible."); cleanup(); return; }
        f.write(QByteArray::fromBase64(b64.toUtf8())); f.close();
        zips.insert(it.key(), tmpZip);
    }

    if (zips.isEmpty()) {
        // Aucun fichier → publier seulement le manifeste (dossier vide).
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
        if (params.value("passwordChanged").toBool()) {
            const QString pwd = params.value("password").toString();
            s.passwordHash = pwd.isEmpty() ? QString() : sha256Hex(pwd);
        }

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
