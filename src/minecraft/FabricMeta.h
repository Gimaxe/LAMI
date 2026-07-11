#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "minecraft/VersionInfo.h"  // Library

class QNetworkAccessManager;

namespace lami {

// Ce que le mod loader Fabric ajoute/remplace par-dessus la version vanilla.
struct FabricProfile {
    QString          mainClass;   // remplace la mainClass vanilla (KnotClient)
    QVector<Library> libraries;   // libs Fabric à ajouter au classpath
    QStringList      jvmArgs;     // args JVM additionnels de Fabric
    bool             valid = false;
};

// Résout un profil de loader « Fabric-like » (même format d'API meta) :
// Fabric (meta.fabricmc.net) ET Quilt (meta.quiltmc.org). loaderVersion vide =>
// dernière disponible. Lecture seule, sans compte → testable headless.
class FabricMeta : public QObject
{
    Q_OBJECT

public:
    explicit FabricMeta(QObject *parent = nullptr);

    // Bases d'API des loaders Fabric-like (se terminent par ".../loader/").
    static QString fabricBase();
    static QString quiltBase();

    // metaBase vide => Fabric par défaut.
    void resolve(const QString &mcVersion,
                 const QString &loaderVersion = QString(),
                 const QString &metaBase = QString());

signals:
    void resolved(const lami::FabricProfile &profile);
    void errorOccurred(const QString &message);

private:
    void fetchProfile(const QString &mcVersion, const QString &loaderVersion);

    QNetworkAccessManager *m_net;
    QString m_metaBase;   // base d'API en cours (Fabric ou Quilt)
};

// Convertit une coordonnée Maven "group:artifact:version[:classifier]" en
// chemin relatif de jar (group/artifact/version/artifact-version[-classifier].jar).
QString mavenCoordToPath(const QString &coord);

} // namespace lami
