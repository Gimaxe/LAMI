#include "minecraft/JavaProvisioner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "minecraft/Downloader.h"

namespace lami {

namespace {
// Manifeste global des runtimes Java de Mojang (liste par OS + composant).
const char *kJavaAllUrl =
    "https://piston-meta.mojang.com/v1/products/java-runtime/"
    "2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
} // namespace

QString JavaProvisioner::osKey()
{
#if defined(Q_OS_WIN)
    return "windows-x64";
#elif defined(Q_OS_MACOS)
#  if defined(Q_PROCESSOR_ARM)
    return "mac-os-arm64";
#  else
    return "mac-os";
#  endif
#else
    return "linux";
#endif
}

JavaProvisioner::JavaProvisioner(QString dataRoot, QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_dataRoot(std::move(dataRoot))
{
}

QString JavaProvisioner::runtimeDir(const QString &component) const
{
    return QDir(m_dataRoot).filePath("java/" + component);
}

QString JavaProvisioner::existingJava(const QString &component) const
{
    const QString dir = runtimeDir(component);
    for (const QString &rel : {QStringLiteral("bin/java.exe"), QStringLiteral("bin/java"),
                               QStringLiteral("jre.bundle/Contents/Home/bin/java")}) {
        const QString p = QDir(dir).filePath(rel);
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

void JavaProvisioner::provision(const QString &component)
{
    // Déjà installé ? on ne retélécharge pas.
    const QString already = existingJava(component);
    if (!already.isEmpty()) {
        emit ready(already);
        return;
    }

    emit progress("Préparation de Java (" + component + ")…");

    QNetworkRequest req{QUrl(kJavaAllUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, component]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Manifeste Java inaccessible : " + reply->errorString());
            return;
        }
        const QJsonObject all = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray entries = all.value(osKey()).toObject().value(component).toArray();
        if (entries.isEmpty()) {
            emit errorOccurred(QStringLiteral("Aucun runtime Java « %1 » pour %2.")
                                   .arg(component, osKey()));
            return;
        }
        const QString manifestUrl =
            entries.first().toObject().value("manifest").toObject().value("url").toString();

        // 2) manifeste détaillé des fichiers du JRE.
        QNetworkRequest mreq{QUrl(manifestUrl)};
        mreq.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
        QNetworkReply *mreply = m_net->get(mreq);
        connect(mreply, &QNetworkReply::finished, this, [this, mreply, component]() {
            mreply->deleteLater();
            if (mreply->error() != QNetworkReply::NoError) {
                emit errorOccurred("Manifeste JRE inaccessible : " + mreply->errorString());
                return;
            }
            const QJsonObject files =
                QJsonDocument::fromJson(mreply->readAll()).object().value("files").toObject();

            const QString dir = runtimeDir(component);
            QVector<DownloadTask> tasks;
            QStringList executables;   // chemins absolus à rendre exécutables
            QString javaPath;

            for (auto it = files.begin(); it != files.end(); ++it) {
                const QString rel = it.key();
                const QJsonObject f = it.value().toObject();
                if (f.value("type").toString() != "file")
                    continue;
                const QJsonObject raw = f.value("downloads").toObject().value("raw").toObject();
                if (raw.isEmpty())
                    continue;

                DownloadTask t;
                t.url = raw.value("url").toString();
                t.dest = QDir(dir).filePath(rel);
                t.expectedHash = raw.value("sha1").toString();  // SHA1
                t.size = static_cast<qint64>(raw.value("size").toDouble(0));
                tasks << t;

                if (f.value("executable").toBool())
                    executables << t.dest;
                if (rel.endsWith("bin/java") || rel.endsWith("bin/java.exe"))
                    javaPath = t.dest;
            }

            if (javaPath.isEmpty()) {
                emit errorOccurred("Exécutable Java introuvable dans le manifeste.");
                return;
            }

            auto *dl = new Downloader(6, this);
            connect(dl, &Downloader::progress, this, [this](int done, int total) {
                emit progress(QStringLiteral("Java : %1/%2 fichiers").arg(done).arg(total));
            });
            connect(dl, &Downloader::finished, this,
                    [this, dl, executables, javaPath](int, int failed) {
                dl->deleteLater();
                if (failed > 0) {
                    emit errorOccurred(QStringLiteral("Téléchargement de Java incomplet (%1 échec).")
                                           .arg(failed));
                    return;
                }
                // Rendre exécutables les binaires (Unix/Mac).
                for (const QString &exe : executables) {
                    QFile f(exe);
                    f.setPermissions(f.permissions() | QFileDevice::ExeOwner
                                     | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                }
                emit ready(javaPath);
            });
            dl->start(tasks);
        });
    });
}

} // namespace lami
