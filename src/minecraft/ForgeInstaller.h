#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "minecraft/VersionInfo.h"  // Library

class QNetworkAccessManager;

namespace lami {

// Ce que Forge / NeoForge ajoutent par-dessus la version vanilla, une fois
// l'installeur officiel exécuté (il patche le client via ses « processors »).
struct ForgeProfile {
    QString          mainClass;   // remplace la mainClass vanilla
    QVector<Library> libraries;   // libs Forge (url vide = déjà produite localement)
    QStringList      jvmArgs;      // args JVM additionnels (module path, etc.)
    QStringList      gameArgs;     // args de jeu additionnels
    bool             valid = false;
};

// Résout un profil Forge/NeoForge en lançant l'installeur officiel en mode
// headless (java -jar installer.jar --installClient <dataRoot>), puis en lisant
// le profil de version généré. Nécessite un java fonctionnel (fourni par le
// JavaProvisioner) et un accès réseau. Idempotent : si déjà installé, réutilise.
class ForgeInstaller : public QObject
{
    Q_OBJECT

public:
    ForgeInstaller(QString dataRoot, QString javaPath, QObject *parent = nullptr);

    // loader = "forge" ou "neoforge". loaderVersion vide => dernière disponible.
    void resolve(const QString &mcVersion, const QString &loader,
                 const QString &loaderVersion = QString());

signals:
    void progress(const QString &step);
    void resolved(const lami::ForgeProfile &profile);
    void errorOccurred(const QString &message);

private:
    void resolveVersionThenInstall(const QString &mcVersion, const QString &loader,
                                   const QString &loaderVersion);
    void runInstaller(const QString &mcVersion, const QString &loader,
                      const QString &loaderVersion);
    ForgeProfile parseProfile(const QString &profileJsonPath) const;

    QString installerUrl(const QString &mcVersion, const QString &loader,
                         const QString &loaderVersion) const;
    QString expectedVersionId(const QString &mcVersion, const QString &loader,
                              const QString &loaderVersion) const;
    QString findInstalledProfile(const QString &loader, const QString &expectedId) const;

    QNetworkAccessManager *m_net;
    QString m_dataRoot;
    QString m_javaPath;
};

} // namespace lami
