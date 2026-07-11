#include "minecraft/FabricMeta.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace lami {

QString mavenCoordToPath(const QString &coord)
{
    const QStringList parts = coord.split(':');
    if (parts.size() < 3)
        return {};
    const QString group    = QString(parts[0]).replace('.', '/');
    const QString artifact = parts[1];
    const QString version  = parts[2];
    const QString classifier = (parts.size() > 3) ? ("-" + parts[3]) : QString();
    return QString("%1/%2/%3/%2-%3%4.jar").arg(group, artifact, version, classifier);
}

namespace {
QString joinUrl(const QString &base, const QString &path)
{
    return base.endsWith('/') ? base + path : base + "/" + path;
}

FabricProfile parseProfile(const QJsonObject &o)
{
    FabricProfile p;
    p.mainClass = o.value("mainClass").toString();

    for (const QJsonValue &lv : o.value("libraries").toArray()) {
        const QJsonObject lib = lv.toObject();
        const QString name = lib.value("name").toString();
        const QString url  = lib.value("url").toString();
        if (name.isEmpty() || url.isEmpty())
            continue;
        const QString path = mavenCoordToPath(name);
        if (path.isEmpty())
            continue;

        Library l;
        l.name = name;
        l.path = path;
        l.url  = joinUrl(url, path);
        l.sha1 = lib.value("sha1").toString();   // souvent absent côté Fabric
        l.size = static_cast<qint64>(lib.value("size").toDouble(0));
        p.libraries.push_back(l);
    }

    // Args JVM additionnels (chaînes simples côté Fabric).
    for (const QJsonValue &av : o.value("arguments").toObject().value("jvm").toArray())
        if (av.isString())
            p.jvmArgs << av.toString();

    p.valid = !p.mainClass.isEmpty() && !p.libraries.isEmpty();
    return p;
}
} // namespace

QString FabricMeta::fabricBase() { return "https://meta.fabricmc.net/v2/versions/loader/"; }
QString FabricMeta::quiltBase()  { return "https://meta.quiltmc.org/v3/versions/loader/"; }

FabricMeta::FabricMeta(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

void FabricMeta::resolve(const QString &mcVersion, const QString &loaderVersion,
                         const QString &metaBase)
{
    m_metaBase = metaBase.isEmpty() ? fabricBase() : metaBase;

    if (!loaderVersion.isEmpty()) {
        fetchProfile(mcVersion, loaderVersion);
        return;
    }

    // Pas de version de loader précisée : on prend la dernière disponible.
    QNetworkRequest req{QUrl(m_metaBase + mcVersion)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, mcVersion]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Liste des loaders Fabric inaccessible : " + reply->errorString());
            return;
        }
        const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        if (arr.isEmpty()) {
            emit errorOccurred("Aucun loader Fabric pour Minecraft " + mcVersion + ".");
            return;
        }
        const QString latest =
            arr.first().toObject().value("loader").toObject().value("version").toString();
        fetchProfile(mcVersion, latest);
    });
}

void FabricMeta::fetchProfile(const QString &mcVersion, const QString &loaderVersion)
{
    const QString url = QString("%1%2/%3/profile/json")
                            .arg(m_metaBase, mcVersion, loaderVersion);
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, mcVersion, loaderVersion]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(QString("Profil Fabric %1/%2 inaccessible : %3")
                                   .arg(mcVersion, loaderVersion, reply->errorString()));
            return;
        }
        const FabricProfile p = parseProfile(QJsonDocument::fromJson(reply->readAll()).object());
        if (!p.valid) {
            emit errorOccurred("Profil Fabric incomplet (mainClass/libraries).");
            return;
        }
        emit resolved(p);
    });
}

} // namespace lami
