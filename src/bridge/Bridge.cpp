#include "bridge/Bridge.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QStandardPaths>
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

QJsonObject serverToUiJson(const ServerInfo &s)
{
    QJsonArray mods;
    for (const ModEntry &m : s.mods)
        mods.append(m.file);

    const QString loader = s.loaderVersion.isEmpty()
        ? s.loader
        : QStringLiteral("%1 (%2)").arg(s.loader, s.loaderVersion);

    return QJsonObject{
        {"id", s.id},
        {"name", s.name},
        {"ip", s.address},
        {"version", s.minecraftVersion},
        {"loader", loader.isEmpty() ? QStringLiteral("Vanilla") : loader},
        {"mods", mods},
        // Champs présents dans l'UI, pas encore gérés par le backend :
        {"plugins", QJsonArray{}},
        {"resourcePacks", QJsonArray{}},
        {"shaders", QJsonArray{}},
        {"installed", false},
    };
}

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , m_gh(new GitHubClient(config::owner(), config::repo(), config::branch(), this))
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
    } else if (method == "login") {
        login(id);
    } else if (method == "startDownload") {
        startDownload(id, params);
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

void Bridge::login(int id)
{
    const QString clientId = config::clientId();
    if (clientId.isEmpty()) {
        replyError(id, "client_id Azure manquant (~/LAMI/.client_id).");
        return;
    }
    if (m_auth) {
        replyError(id, "Connexion déjà en cours.");
        return;
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

void Bridge::startDownload(int id, const QJsonObject &params)
{
    const QString serverId = params.value("id").toString();
    if (serverId.isEmpty()) {
        replyError(id, "Identifiant de serveur manquant.");
        return;
    }

    auto *mgr = new InstanceManager(config::owner(), config::repo(), config::branch(),
                                    config::token(), config::dataRoot(), config::javaPath(), this);

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

        connect(dl, &Downloader::progress, this, [this, serverId](int done, int total) {
            emit event(QJsonObject{{"event", "downloadProgress"}, {"id", serverId},
                                   {"done", done}, {"total", total},
                                   {"percent", total > 0 ? (done * 100 / total) : 0}});
        });
        connect(dl, &Downloader::finished, this,
                [this, id, serverId, plan, mgr, dl](int ok, int failed) {
            // Marque les mods du repo comme gérés par le launcher (sync non-destructif).
            SyncManager sync(plan.gameDir);
            QStringList modPaths;
            for (const ModEntry &m : plan.server.mods)
                modPaths << modLocalPath(m);
            if (!modPaths.isEmpty())
                sync.markInstalled(modPaths);

            replyOk(id, QJsonObject{{"id", serverId}, {"downloaded", ok}, {"failed", failed}});
            emit event(QJsonObject{{"event", "downloadDone"}, {"id", serverId}, {"failed", failed}});
            dl->deleteLater();
            mgr->deleteLater();
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

    // Le zip de mods (octets base64) est fourni par le JS (webview : pas de chemin).
    const QString b64 = params.value("modsZip").toString();
    const QString tmpBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (!b64.isEmpty()) {
        const QByteArray zip = QByteArray::fromBase64(b64.toUtf8());
        const QString tmpZip = QDir(tmpBase).filePath("lami-pub-" + sid + ".zip");
        QFile f(tmpZip);
        if (!f.open(QIODevice::WriteOnly)) { replyError(id, "Écriture temporaire impossible."); cleanup(); return; }
        f.write(zip); f.close();
        pub->publishFromZip(srv, tmpZip, m_session.uuid);
    } else {
        // Aucun mod fourni → on publie seulement le manifeste (dossier vide).
        const QString tmpDir = QDir(tmpBase).filePath("lami-pub-empty-" + sid);
        QDir(tmpDir).removeRecursively();
        QDir().mkpath(tmpDir);
        pub->publishFromFolder(srv, tmpDir, m_session.uuid);
    }
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
