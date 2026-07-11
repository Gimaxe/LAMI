#include "minecraft/ForgeInstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <utility>

#include "minecraft/FabricMeta.h"  // mavenCoordToPath

namespace lami {

namespace {
const char *kForgePromos =
    "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json";
const char *kNeoMetadata =
    "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml";

// GET simple : appelle onOk(bytes) ou onErr(message).
template <typename OkFn, typename ErrFn>
void httpGet(QNetworkAccessManager *net, const QString &url, QObject *ctx,
             OkFn onOk, ErrFn onErr)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LAMI-Launcher");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = net->get(req);
    QObject::connect(reply, &QNetworkReply::finished, ctx, [reply, onOk, onErr]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            onErr(reply->errorString());
            return;
        }
        onOk(reply->readAll());
    });
}
} // namespace

ForgeInstaller::ForgeInstaller(QString dataRoot, QString javaPath, QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_dataRoot(std::move(dataRoot))
    , m_javaPath(std::move(javaPath))
{
}

void ForgeInstaller::resolve(const QString &mcVersion, const QString &loader,
                             const QString &loaderVersion)
{
    const QString l = loader.trimmed().toLower();
    if (l != "forge" && l != "neoforge") {
        emit errorOccurred("Loader inconnu : " + loader);
        return;
    }
    if (!loaderVersion.trimmed().isEmpty()) {
        runInstaller(mcVersion, l, loaderVersion.trimmed());
        return;
    }
    resolveVersionThenInstall(mcVersion, l, QString());
}

void ForgeInstaller::resolveVersionThenInstall(const QString &mcVersion,
                                               const QString &loader,
                                               const QString &)
{
    emit progress(QString("Recherche de la dernière version de %1 pour %2…")
                      .arg(loader == "neoforge" ? "NeoForge" : "Forge", mcVersion));

    if (loader == "forge") {
        httpGet(m_net, kForgePromos, this,
            [this, mcVersion, loader](const QByteArray &data) {
                const QJsonObject promos =
                    QJsonDocument::fromJson(data).object().value("promos").toObject();
                // On préfère "recommended", sinon "latest".
                QString v = promos.value(mcVersion + "-recommended").toString();
                if (v.isEmpty()) v = promos.value(mcVersion + "-latest").toString();
                if (v.isEmpty()) {
                    emit errorOccurred("Aucune version Forge trouvée pour " + mcVersion);
                    return;
                }
                runInstaller(mcVersion, loader, v);
            },
            [this](const QString &e) {
                emit errorOccurred("Promotions Forge inaccessibles : " + e);
            });
        return;
    }

    // NeoForge : versions type 21.1.80 (dérivées de 1.21.1). On filtre le
    // maven-metadata sur le préfixe correspondant à la version Minecraft.
    httpGet(m_net, kNeoMetadata, this,
        [this, mcVersion, loader](const QByteArray &data) {
            const QString xml = QString::fromUtf8(data);
            QRegularExpression re("<version>([^<]+)</version>");
            QStringList all;
            auto it = re.globalMatch(xml);
            while (it.hasNext()) all << it.next().captured(1);
            if (all.isEmpty()) {
                emit errorOccurred("maven-metadata NeoForge vide.");
                return;
            }
            // 1.21.1 -> préfixe "21.1." ; 1.21 -> "21.0."
            const QStringList parts = mcVersion.split('.');
            QString prefix;
            if (parts.size() >= 2) {
                const QString minor = parts.value(1);
                const QString patch = parts.size() >= 3 ? parts.value(2) : QStringLiteral("0");
                prefix = minor + "." + patch + ".";
            }
            QString best;
            for (const QString &v : all) {
                if (!prefix.isEmpty() && !v.startsWith(prefix)) continue;
                if (v.contains("beta")) continue;  // on évite les beta si possible
                best = v;  // maven-metadata est trié croissant → le dernier gagne
            }
            if (best.isEmpty())  // repli : dernière tout court
                best = all.last();
            runInstaller(mcVersion, loader, best);
        },
        [this](const QString &e) {
            emit errorOccurred("maven-metadata NeoForge inaccessible : " + e);
        });
}

QString ForgeInstaller::installerUrl(const QString &mcVersion, const QString &loader,
                                     const QString &loaderVersion) const
{
    if (loader == "neoforge") {
        return QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/"
                              "neoforge/%1/neoforge-%1-installer.jar")
            .arg(loaderVersion);
    }
    const QString full = mcVersion + "-" + loaderVersion;
    return QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/"
                          "%1/forge-%1-installer.jar")
        .arg(full);
}

QString ForgeInstaller::expectedVersionId(const QString &mcVersion, const QString &loader,
                                          const QString &loaderVersion) const
{
    if (loader == "neoforge")
        return "neoforge-" + loaderVersion;
    return mcVersion + "-forge-" + loaderVersion;
}

QString ForgeInstaller::findInstalledProfile(const QString &loader,
                                             const QString &expectedId) const
{
    const QDir versions(QDir(m_dataRoot).filePath("versions"));
    // Chemin attendu en premier.
    const QString direct = versions.filePath(expectedId + "/" + expectedId + ".json");
    if (QFile::exists(direct)) return direct;

    // Sinon on cherche un profil dont le nom contient le loader (le plus récent).
    const QString needle = loader == "neoforge" ? "neoforge" : "forge";
    QString best;
    QDateTime bestTime;
    for (const QFileInfo &d : versions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!d.fileName().toLower().contains(needle)) continue;
        const QString json = d.absoluteFilePath() + "/" + d.fileName() + ".json";
        if (!QFile::exists(json)) continue;
        if (best.isEmpty() || d.lastModified() > bestTime) {
            best = json;
            bestTime = d.lastModified();
        }
    }
    return best;
}

void ForgeInstaller::runInstaller(const QString &mcVersion, const QString &loader,
                                  const QString &loaderVersion)
{
    const QString expectedId = expectedVersionId(mcVersion, loader, loaderVersion);

    // Déjà installé ? On saute directement à la lecture du profil (idempotent).
    const QString existing = findInstalledProfile(loader, expectedId);
    if (!existing.isEmpty()) {
        emit progress(QString("%1 %2 déjà installé.")
                          .arg(loader == "neoforge" ? "NeoForge" : "Forge", loaderVersion));
        const ForgeProfile p = parseProfile(existing);
        if (p.valid) { emit resolved(p); return; }
        // profil illisible → on réinstalle
    }

    QDir().mkpath(m_dataRoot);
    // L'installeur exige un launcher_profiles.json dans le dossier cible.
    const QString profilesPath = QDir(m_dataRoot).filePath("launcher_profiles.json");
    if (!QFile::exists(profilesPath)) {
        QFile f(profilesPath);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(QJsonObject{{"profiles", QJsonObject{}}}).toJson());
    }

    const QString url = installerUrl(mcVersion, loader, loaderVersion);
    emit progress(QString("Téléchargement de l'installeur %1 %2…")
                      .arg(loader == "neoforge" ? "NeoForge" : "Forge", loaderVersion));

    httpGet(m_net, url, this,
        [this, loader, expectedId](const QByteArray &jar) {
            const QString cacheDir = QDir(m_dataRoot).filePath(".forge-cache");
            QDir().mkpath(cacheDir);
            const QString jarPath = QDir(cacheDir).filePath("installer.jar");
            QFile f(jarPath);
            if (!f.open(QIODevice::WriteOnly) || f.write(jar) != jar.size()) {
                emit errorOccurred("Écriture de l'installeur impossible.");
                return;
            }
            f.close();

            emit progress("Exécution de l'installeur (patch du client)…");
            auto *proc = new QProcess(this);
            proc->setProcessChannelMode(QProcess::MergedChannels);
            proc->setWorkingDirectory(m_dataRoot);
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, proc, loader, expectedId](int code, QProcess::ExitStatus) {
                        const QString out = QString::fromUtf8(proc->readAll());
                        proc->deleteLater();
                        if (code != 0) {
                            emit errorOccurred("Installeur en échec (code " +
                                               QString::number(code) + ").\n" +
                                               out.right(500));
                            return;
                        }
                        const QString profile = findInstalledProfile(loader, expectedId);
                        if (profile.isEmpty()) {
                            emit errorOccurred("Profil de version introuvable après installation.");
                            return;
                        }
                        const ForgeProfile p = parseProfile(profile);
                        if (!p.valid) {
                            emit errorOccurred("Profil de version illisible : " + profile);
                            return;
                        }
                        emit progress("Installeur terminé.");
                        emit resolved(p);
                    });
            connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
                emit errorOccurred("Impossible de lancer java pour l'installeur : " +
                                   proc->errorString());
                proc->deleteLater();
            });
            proc->start(m_javaPath, {"-jar", jarPath, "--installClient", m_dataRoot});
        },
        [this](const QString &e) {
            emit errorOccurred("Téléchargement de l'installeur échoué : " + e);
        });
}

ForgeProfile ForgeInstaller::parseProfile(const QString &profileJsonPath) const
{
    ForgeProfile prof;
    QFile f(profileJsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return prof;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();

    prof.mainClass = obj.value("mainClass").toString();

    // Libraries : Forge fournit downloads.artifact (path/url/sha1/size). Certaines
    // libs sont produites localement par les processors (url vide) → on les garde
    // pour le classpath mais elles ne seront pas re-téléchargées.
    for (const QJsonValue &lv : obj.value("libraries").toArray()) {
        const QJsonObject lo = lv.toObject();
        const QString name = lo.value("name").toString();
        if (name.isEmpty()) continue;
        const QJsonObject art =
            lo.value("downloads").toObject().value("artifact").toObject();
        Library lib;
        lib.name = name;
        lib.path = art.value("path").toString();
        if (lib.path.isEmpty()) lib.path = mavenCoordToPath(name);
        lib.url  = art.value("url").toString();
        lib.sha1 = art.value("sha1").toString();
        lib.size = static_cast<qint64>(art.value("size").toDouble(0));
        prof.libraries << lib;
    }

    // Arguments (format 1.13+). On ne prend que les tokens « simples » (chaînes) ;
    // les tokens conditionnels (objets avec rules) sont ignorés par prudence.
    const QJsonObject args = obj.value("arguments").toObject();
    auto collect = [](const QJsonArray &arr, QStringList &out) {
        for (const QJsonValue &v : arr)
            if (v.isString()) out << v.toString();
    };
    collect(args.value("jvm").toArray(), prof.jvmArgs);
    collect(args.value("game").toArray(), prof.gameArgs);

    prof.valid = !prof.mainClass.isEmpty();
    return prof;
}

} // namespace lami
