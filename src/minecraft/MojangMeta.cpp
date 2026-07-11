#include "minecraft/MojangMeta.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace lami {

namespace {
const char *kManifestUrl =
    "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json";

// Évalue les "rules" Mojang pour l'OS courant (allow/disallow selon os.name).
bool rulesAllow(const QJsonArray &rules)
{
    if (rules.isEmpty())
        return true;

    bool allowed = false;  // par défaut refusé dès qu'il y a des règles
    const QString os = currentOsName();
    for (const QJsonValue &rv : rules) {
        const QJsonObject rule = rv.toObject();
        bool applies = true;
        if (rule.contains("os")) {
            const QString name = rule.value("os").toObject().value("name").toString();
            if (!name.isEmpty())
                applies = (name == os);
        }
        if (applies)
            allowed = (rule.value("action").toString() == "allow");
    }
    return allowed;
}

// Évalue les rules d'un élément d'argument. Les arguments conditionnés par des
// "features" (démo, résolution custom…) sont ignorés tant qu'on ne les gère pas.
bool argRulesAllow(const QJsonArray &rules)
{
    if (rules.isEmpty())
        return true;
    bool allowed = false;
    const QString os = currentOsName();
    for (const QJsonValue &rv : rules) {
        const QJsonObject rule = rv.toObject();
        if (rule.contains("features"))
            return false;  // arg conditionné par une feature non supportée
        bool applies = true;
        if (rule.contains("os")) {
            const QString name = rule.value("os").toObject().value("name").toString();
            if (!name.isEmpty())
                applies = (name == os);
        }
        if (applies)
            allowed = (rule.value("action").toString() == "allow");
    }
    return allowed;
}

// Aplati une des listes d'arguments ("jvm" ou "game") en gabarits, OS-filtrés.
QStringList parseArgArray(const QJsonArray &arr)
{
    QStringList out;
    for (const QJsonValue &v : arr) {
        if (v.isString()) {
            out << v.toString();
        } else if (v.isObject()) {
            const QJsonObject obj = v.toObject();
            if (!argRulesAllow(obj.value("rules").toArray()))
                continue;
            const QJsonValue value = obj.value("value");
            if (value.isString())
                out << value.toString();
            else
                for (const QJsonValue &vv : value.toArray())
                    out << vv.toString();
        }
    }
    return out;
}

VersionInfo parseVersion(const QJsonObject &o, const QString &id)
{
    VersionInfo v;
    v.id        = o.value("id").toString(id);
    v.type      = o.value("type").toString();
    v.mainClass = o.value("mainClass").toString();

    const QJsonObject assetIndex = o.value("assetIndex").toObject();
    v.assetIndexId   = assetIndex.value("id").toString();
    v.assetIndexUrl  = assetIndex.value("url").toString();
    v.assetIndexSha1 = assetIndex.value("sha1").toString();

    const QJsonObject client = o.value("downloads").toObject().value("client").toObject();
    v.clientUrl  = client.value("url").toString();
    v.clientSha1 = client.value("sha1").toString();
    v.clientSize = static_cast<qint64>(client.value("size").toDouble(0));

    // Libraries : on garde celles autorisées pour l'OS courant, avec un artifact.
    for (const QJsonValue &lv : o.value("libraries").toArray()) {
        const QJsonObject lib = lv.toObject();
        if (!rulesAllow(lib.value("rules").toArray()))
            continue;

        const QJsonObject artifact =
            lib.value("downloads").toObject().value("artifact").toObject();
        if (artifact.isEmpty())
            continue;  // (natives/classifiers : gérés plus tard)

        Library l;
        l.name = lib.value("name").toString();
        l.path = artifact.value("path").toString();
        l.url  = artifact.value("url").toString();
        l.sha1 = artifact.value("sha1").toString();
        l.size = static_cast<qint64>(artifact.value("size").toDouble(0));
        if (!l.url.isEmpty())
            v.libraries.push_back(l);
    }

    // Arguments : format moderne ("arguments") ou repli pré-1.13 ("minecraftArguments").
    const QJsonObject args = o.value("arguments").toObject();
    if (!args.isEmpty()) {
        v.jvmArgs  = parseArgArray(args.value("jvm").toArray());
        v.gameArgs = parseArgArray(args.value("game").toArray());
    } else if (o.contains("minecraftArguments")) {
        v.gameArgs = o.value("minecraftArguments").toString()
                         .split(' ', Qt::SkipEmptyParts);
        v.jvmArgs  = {"-Djava.library.path=${natives_directory}", "-cp", "${classpath}"};
    }

    v.valid = !v.id.isEmpty() && !v.mainClass.isEmpty() && !v.clientUrl.isEmpty();
    return v;
}
} // namespace

MojangMeta::MojangMeta(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

void MojangMeta::resolve(const QString &mcVersion)
{
    QNetworkRequest req{QUrl(kManifestUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, mcVersion]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Manifeste Mojang inaccessible : " + reply->errorString());
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();

        QString wanted = mcVersion;
        if (wanted.isEmpty() || wanted == "latest")
            wanted = root.value("latest").toObject().value("release").toString();

        for (const QJsonValue &vv : root.value("versions").toArray()) {
            const QJsonObject ver = vv.toObject();
            if (ver.value("id").toString() == wanted) {
                fetchVersionJson(ver.value("url").toString(), wanted);
                return;
            }
        }
        emit errorOccurred(QString("Version Minecraft « %1 » introuvable.").arg(wanted));
    });
}

void MojangMeta::fetchVersionJson(const QString &url, const QString &id)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("JSON de version inaccessible : " + reply->errorString());
            return;
        }

        const VersionInfo info = parseVersion(
            QJsonDocument::fromJson(reply->readAll()).object(), id);
        if (!info.valid) {
            emit errorOccurred("JSON de version incomplet (id/mainClass/client manquant).");
            return;
        }
        emit resolved(info);
    });
}

} // namespace lami
