#include "github/GitHubClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <memory>
#include <utility>

namespace lami {

namespace {

// Parse le JSON d'un servers/<id>.json en ServerInfo.
// Tolérant : accepte quelques alias de clés (camelCase / snake_case).
ServerInfo parseServer(const QJsonObject &obj, const QString &fallbackId)
{
    ServerInfo s;
    s.id      = obj.value("id").toString(fallbackId);
    s.name    = obj.value("name").toString(s.id);
    s.address = obj.value("address").toString();

    s.minecraftVersion = obj.contains("minecraft_version")
        ? obj.value("minecraft_version").toString()
        : obj.value("minecraftVersion").toString();

    s.loader = obj.value("loader").toString("vanilla");

    s.loaderVersion = obj.contains("loader_version")
        ? obj.value("loader_version").toString()
        : obj.value("loaderVersion").toString();

    // Parse les 4 catégories d'assets.
    for (const char *type : {assets::Mods, assets::Plugins, assets::ResourcePacks, assets::Shaders}) {
        QVector<ModEntry> &list = s.assetList(type);
        for (const QJsonValue &v : obj.value(type).toArray()) {
            const QJsonObject m = v.toObject();
            ModEntry entry;
            entry.file   = m.value("file").toString();
            entry.sha256 = m.value("sha256").toString();
            entry.size   = static_cast<qint64>(m.value("size").toDouble(0));
            if (!entry.file.isEmpty())
                list.push_back(entry);
        }
    }

    s.passwordHash = obj.value("password_hash").toString();

    // Un serveur est valide s'il a au moins un id et une version.
    s.valid = !s.id.isEmpty() && !s.minecraftVersion.isEmpty();
    return s;
}

RoleTable parseRoles(const QJsonObject &root)
{
    // Accepte soit { "roles": { uuid: role } }, soit directement { uuid: role }.
    const QJsonObject map = root.contains("roles")
        ? root.value("roles").toObject()
        : root;

    RoleTable table;
    for (auto it = map.begin(); it != map.end(); ++it)
        table.insert(it.key(), roleFromString(it.value().toString()));
    return table;
}

} // namespace

GitHubClient::GitHubClient(QString owner, QString repo, QString branch, QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_owner(std::move(owner))
    , m_repo(std::move(repo))
    , m_branch(std::move(branch))
{
}

void GitHubClient::setToken(const QString &token)
{
    m_token = token.trimmed();
}

QString GitHubClient::rawUrl(const QString &path) const
{
    return QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3/%4")
        .arg(m_owner, m_repo, m_branch, path);
}

QString GitHubClient::apiContentsUrl(const QString &path) const
{
    return QStringLiteral("https://api.github.com/repos/%1/%2/contents/%3?ref=%4")
        .arg(m_owner, m_repo, path, m_branch);
}

QString GitHubClient::apiContentsUrlNoRef(const QString &path) const
{
    return QStringLiteral("https://api.github.com/repos/%1/%2/contents/%3")
        .arg(m_owner, m_repo, path);
}

namespace {
// Applique les en-têtes d'API GitHub authentifiée (métadonnées JSON).
void applyApiHeaders(QNetworkRequest &req, const QString &token)
{
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
}
} // namespace

void GitHubClient::putFile(const QString &path, const QByteArray &content,
                           const QString &commitMessage)
{
    if (m_token.isEmpty()) {
        emit writeError(tr("Écriture impossible : aucun token (rôle Hébergeur/Admin requis)."));
        return;
    }

    // 1) Récupérer le sha existant (nécessaire pour une mise à jour ; absent = création).
    QNetworkRequest getReq{QUrl(apiContentsUrl(path))};
    applyApiHeaders(getReq, m_token);
    QNetworkReply *getReply = m_net->get(getReq);

    connect(getReply, &QNetworkReply::finished, this,
            [this, getReply, path, content, commitMessage]() {
        getReply->deleteLater();
        QString sha;
        if (getReply->error() == QNetworkReply::NoError)
            sha = QJsonDocument::fromJson(getReply->readAll()).object().value("sha").toString();

        QJsonObject body{
            {"message", commitMessage},
            {"content", QString::fromLatin1(content.toBase64())},
            {"branch", m_branch},
        };
        if (!sha.isEmpty())
            body.insert("sha", sha);  // mise à jour d'un fichier existant

        QNetworkRequest putReq{QUrl(apiContentsUrlNoRef(path))};
        applyApiHeaders(putReq, m_token);
        putReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *putReply =
            m_net->put(putReq, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(putReply, &QNetworkReply::finished, this, [this, putReply, path]() {
            putReply->deleteLater();
            if (putReply->error() != QNetworkReply::NoError) {
                emit writeError(tr("Publication de %1 échouée : %2 — %3")
                                    .arg(path, putReply->errorString(),
                                         QString::fromUtf8(putReply->readAll().left(200))));
                return;
            }
            emit filePut(path);
        });
    });
}

void GitHubClient::uploadIfAbsent(const QString &path, const QByteArray &content,
                                  const QString &commitMessage)
{
    if (m_token.isEmpty()) {
        emit writeError(tr("Upload impossible : aucun token."));
        return;
    }

    // 1) Existe déjà dans la banque ? → on saute (dédoublonnage).
    QNetworkRequest getReq{QUrl(apiContentsUrl(path))};
    applyApiHeaders(getReq, m_token);
    QNetworkReply *getReply = m_net->get(getReq);

    connect(getReply, &QNetworkReply::finished, this,
            [this, getReply, path, content, commitMessage]() {
        getReply->deleteLater();
        if (getReply->error() == QNetworkReply::NoError) {
            emit uploadDone(path, true);   // déjà présent : mutualisé
            return;
        }

        // 2) Absent → création.
        QJsonObject body{
            {"message", commitMessage},
            {"content", QString::fromLatin1(content.toBase64())},
            {"branch", m_branch},
        };
        QNetworkRequest putReq{QUrl(apiContentsUrlNoRef(path))};
        applyApiHeaders(putReq, m_token);
        putReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *putReply =
            m_net->put(putReq, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(putReply, &QNetworkReply::finished, this, [this, putReply, path]() {
            putReply->deleteLater();
            if (putReply->error() != QNetworkReply::NoError) {
                emit writeError(tr("Upload de %1 échoué : %2 — %3")
                                    .arg(path, putReply->errorString(),
                                         QString::fromUtf8(putReply->readAll().left(200))));
                return;
            }
            emit uploadDone(path, false);   // nouvellement ajouté à la banque
        });
    });
}

void GitHubClient::deleteFile(const QString &path, const QString &commitMessage)
{
    if (m_token.isEmpty()) {
        emit writeError(tr("Suppression impossible : aucun token."));
        return;
    }

    QNetworkRequest getReq{QUrl(apiContentsUrl(path))};
    applyApiHeaders(getReq, m_token);
    QNetworkReply *getReply = m_net->get(getReq);

    connect(getReply, &QNetworkReply::finished, this,
            [this, getReply, path, commitMessage]() {
        getReply->deleteLater();
        if (getReply->error() != QNetworkReply::NoError) {
            emit writeError(tr("Suppression : %1 introuvable.").arg(path));
            return;
        }
        const QString sha =
            QJsonDocument::fromJson(getReply->readAll()).object().value("sha").toString();

        QJsonObject body{
            {"message", commitMessage},
            {"sha", sha},
            {"branch", m_branch},
        };

        QNetworkRequest delReq{QUrl(apiContentsUrlNoRef(path))};
        applyApiHeaders(delReq, m_token);
        delReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *delReply = m_net->sendCustomRequest(
            delReq, "DELETE", QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(delReply, &QNetworkReply::finished, this, [this, delReply, path]() {
            delReply->deleteLater();
            if (delReply->error() != QNetworkReply::NoError) {
                emit writeError(tr("Suppression de %1 échouée : %2")
                                    .arg(path, delReply->errorString()));
                return;
            }
            emit fileDeleted(path);
        });
    });
}

QNetworkReply *GitHubClient::get(const QString &path)
{
    // Repo privé (token) → API contents authentifiée, en demandant le contenu brut.
    // Repo public (pas de token) → raw.githubusercontent.com.
    if (!m_token.isEmpty()) {
        QNetworkRequest req{QUrl(apiContentsUrl(path))};
        req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
        req.setRawHeader("Accept", "application/vnd.github.raw+json");
        req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
        return m_net->get(req);
    }

    QNetworkRequest req{QUrl(rawUrl(path))};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    return m_net->get(req);
}

void GitHubClient::fetchServer(const QString &id)
{
    QNetworkReply *reply = get(QStringLiteral("servers/%1.json").arg(id));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(tr("Serveur « %1 » introuvable : %2")
                                   .arg(id, reply->errorString()));
            return;
        }

        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred(tr("servers/%1.json est invalide : %2")
                                   .arg(id, perr.errorString()));
            return;
        }

        const ServerInfo server = parseServer(doc.object(), id);
        if (!server.valid) {
            emit errorOccurred(tr("servers/%1.json : champs obligatoires manquants "
                                  "(id / minecraft_version).").arg(id));
            return;
        }
        emit serverFetched(server);
    });

    return;
}

namespace {
// Normalise un UUID (minuscules, sans tirets) pour comparer deux formes.
QString normUuid(const QString &u) { return QString(u).remove('-').toLower(); }
}

void GitHubClient::setRole(const QString &uuid, const QString &role, const QString &commitMessage)
{
    if (m_token.isEmpty()) { emit writeError(tr("Rôles : aucun token.")); return; }

    QNetworkRequest getReq{QUrl(apiContentsUrl("roles.json"))};
    applyApiHeaders(getReq, m_token);
    QNetworkReply *getReply = m_net->get(getReq);
    connect(getReply, &QNetworkReply::finished, this,
            [this, getReply, uuid, role, commitMessage]() {
        getReply->deleteLater();
        if (getReply->error() != QNetworkReply::NoError) {
            emit writeError(tr("roles.json illisible : %1").arg(getReply->errorString()));
            return;
        }
        const QJsonObject meta = QJsonDocument::fromJson(getReply->readAll()).object();
        const QString sha = meta.value("sha").toString();
        QJsonObject root = QJsonDocument::fromJson(
            QByteArray::fromBase64(meta.value("content").toString().toUtf8())).object();

        QJsonObject roles = root.value("roles").toObject();
        // Écrase une éventuelle entrée équivalente (forme d'UUID différente).
        for (const QString &k : roles.keys())
            if (normUuid(k) == normUuid(uuid))
                roles.remove(k);
        roles.insert(uuid, role);
        root.insert("roles", roles);

        QJsonObject body{
            {"message", commitMessage},
            {"content", QString::fromLatin1(
                            QJsonDocument(root).toJson(QJsonDocument::Indented).toBase64())},
            {"branch", m_branch}, {"sha", sha},
        };
        QNetworkRequest putReq{QUrl(apiContentsUrlNoRef("roles.json"))};
        applyApiHeaders(putReq, m_token);
        putReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *putReply = m_net->put(putReq, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(putReply, &QNetworkReply::finished, this, [this, putReply]() {
            putReply->deleteLater();
            if (putReply->error() != QNetworkReply::NoError)
                emit writeError(tr("Écriture de roles.json échouée : %1").arg(putReply->errorString()));
            else
                emit rolesUpdated();
        });
    });
}

void GitHubClient::removeRole(const QString &uuid, const QString &commitMessage)
{
    if (m_token.isEmpty()) { emit writeError(tr("Rôles : aucun token.")); return; }

    QNetworkRequest getReq{QUrl(apiContentsUrl("roles.json"))};
    applyApiHeaders(getReq, m_token);
    QNetworkReply *getReply = m_net->get(getReq);
    connect(getReply, &QNetworkReply::finished, this, [this, getReply, uuid, commitMessage]() {
        getReply->deleteLater();
        if (getReply->error() != QNetworkReply::NoError) {
            emit writeError(tr("roles.json illisible : %1").arg(getReply->errorString()));
            return;
        }
        const QJsonObject meta = QJsonDocument::fromJson(getReply->readAll()).object();
        const QString sha = meta.value("sha").toString();
        QJsonObject root = QJsonDocument::fromJson(
            QByteArray::fromBase64(meta.value("content").toString().toUtf8())).object();

        QJsonObject roles = root.value("roles").toObject();
        for (const QString &k : roles.keys())
            if (normUuid(k) == normUuid(uuid))
                roles.remove(k);
        root.insert("roles", roles);

        QJsonObject body{
            {"message", commitMessage},
            {"content", QString::fromLatin1(
                            QJsonDocument(root).toJson(QJsonDocument::Indented).toBase64())},
            {"branch", m_branch}, {"sha", sha},
        };
        QNetworkRequest putReq{QUrl(apiContentsUrlNoRef("roles.json"))};
        applyApiHeaders(putReq, m_token);
        putReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *putReply = m_net->put(putReq, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(putReply, &QNetworkReply::finished, this, [this, putReply]() {
            putReply->deleteLater();
            if (putReply->error() != QNetworkReply::NoError)
                emit writeError(tr("Écriture de roles.json échouée : %1").arg(putReply->errorString()));
            else
                emit rolesUpdated();
        });
    });
}

void GitHubClient::fetchServerByAddress(const QString &address)
{
    QNetworkReply *reply = get(QStringLiteral("servers/index.json"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, address]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(tr("Index des serveurs introuvable (servers/index.json)."));
            return;
        }
        const QJsonObject idx = QJsonDocument::fromJson(reply->readAll()).object();
        const QString id = idx.value(address).toString();
        if (id.isEmpty()) {
            emit errorOccurred(tr("Aucun serveur pour l'adresse « %1 ».").arg(address));
            return;
        }
        fetchServer(id);  // réutilise le flux existant
    });
}

void GitHubClient::upsertAddressIndex(const QString &address, const QString &serverId,
                                      const QString &commitMessage)
{
    if (m_token.isEmpty()) {
        emit writeError(tr("Index : aucun token."));
        return;
    }

    // Lecture de l'index existant (métadonnées : sha + contenu base64).
    QNetworkRequest getReq{QUrl(apiContentsUrl("servers/index.json"))};
    applyApiHeaders(getReq, m_token);
    QNetworkReply *getReply = m_net->get(getReq);

    connect(getReply, &QNetworkReply::finished, this,
            [this, getReply, address, serverId, commitMessage]() {
        getReply->deleteLater();

        QString sha;
        QJsonObject index;
        if (getReply->error() == QNetworkReply::NoError) {
            const QJsonObject meta = QJsonDocument::fromJson(getReply->readAll()).object();
            sha = meta.value("sha").toString();
            const QByteArray raw =
                QByteArray::fromBase64(meta.value("content").toString().toUtf8());
            index = QJsonDocument::fromJson(raw).object();
        }
        index.insert(address, serverId);  // ajoute / met à jour l'entrée

        QJsonObject body{
            {"message", commitMessage},
            {"content", QString::fromLatin1(
                            QJsonDocument(index).toJson(QJsonDocument::Indented).toBase64())},
            {"branch", m_branch},
        };
        if (!sha.isEmpty())
            body.insert("sha", sha);

        QNetworkRequest putReq{QUrl(apiContentsUrlNoRef("servers/index.json"))};
        applyApiHeaders(putReq, m_token);
        putReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *putReply =
            m_net->put(putReq, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(putReply, &QNetworkReply::finished, this, [this, putReply]() {
            putReply->deleteLater();
            if (putReply->error() != QNetworkReply::NoError) {
                emit writeError(tr("Mise à jour de l'index échouée : %1")
                                    .arg(putReply->errorString()));
                return;
            }
            emit indexUpdated();
        });
    });
}

void GitHubClient::fetchAllServers()
{
    // 1) Lister le dossier servers/ (l'API contents renvoie un tableau JSON).
    QNetworkRequest req{QUrl(apiContentsUrl("servers"))};
    if (!m_token.isEmpty())
        applyApiHeaders(req, m_token);
    else
        req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(tr("Impossible de lister les serveurs : %1").arg(reply->errorString()));
            return;
        }

        QStringList ids;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QString name = v.toObject().value("name").toString();
            if (name.endsWith(".json") && name != "index.json")
                ids << name.left(name.size() - 5);
        }
        if (ids.isEmpty()) {
            emit serversFetched({});
            return;
        }

        // 2) Charger chaque servers/<id>.json en parallèle, puis émettre le lot.
        auto results   = std::make_shared<QVector<ServerInfo>>();
        auto remaining = std::make_shared<int>(ids.size());
        for (const QString &id : ids) {
            QNetworkReply *r = get(QStringLiteral("servers/%1.json").arg(id));
            connect(r, &QNetworkReply::finished, this, [this, r, id, results, remaining]() {
                r->deleteLater();
                if (r->error() == QNetworkReply::NoError) {
                    const QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
                    if (doc.isObject()) {
                        const ServerInfo s = parseServer(doc.object(), id);
                        if (s.valid)
                            results->push_back(s);
                    }
                }
                if (--(*remaining) == 0)
                    emit serversFetched(*results);
            });
        }
    });
}

void GitHubClient::fetchRoles()
{
    QNetworkReply *reply = get(QStringLiteral("roles.json"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(tr("roles.json introuvable : %1").arg(reply->errorString()));
            return;
        }

        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred(tr("roles.json est invalide : %1").arg(perr.errorString()));
            return;
        }

        emit rolesFetched(parseRoles(doc.object()));
    });
}

} // namespace lami
