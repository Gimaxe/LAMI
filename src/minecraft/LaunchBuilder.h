#pragma once

#include <QString>
#include <QStringList>

#include "auth/MicrosoftAuth.h"   // MinecraftSession
#include "minecraft/VersionInfo.h"

namespace lami {

// Emplacements disque nécessaires pour lancer une instance.
struct LaunchPaths {
    QString javaPath;       // exécutable java (JRE choisi selon la version)
    QString gameDir;        // dossier de jeu isolé du serveur (.minecraft de l'instance)
    QString assetsRoot;     // dossier assets partagé
    QString librariesRoot;  // dossier libraries partagé
    QString nativesDir;     // natives extraites pour cette version
    QString clientJar;      // chemin du client.jar
};

// Construit la ligne de commande de lancement de Minecraft :
//   [java, <jvmArgs>, <mainClass>, <gameArgs>]
// en substituant tous les placeholders ${...} (classpath, token, uuid, pseudo,
// dossiers…). Pur / sans réseau → testable en headless.
class LaunchBuilder
{
public:
    // Chemins absolus de tous les jars du classpath (libraries + client.jar).
    static QStringList classpathEntries(const VersionInfo &version,
                                        const LaunchPaths &paths);

    // Classpath assemblé avec le séparateur de l'OS (':' unix, ';' windows).
    static QString classpath(const VersionInfo &version, const LaunchPaths &paths);

    // Commande complète (argv[0] = java). Prête pour QProcess::start.
    static QStringList build(const VersionInfo &version,
                             const MinecraftSession &session,
                             const LaunchPaths &paths,
                             const QString &launcherName = "LAMI",
                             const QString &launcherVersion = "0.1.0");

    // Séparateur de classpath de l'OS courant.
    static QString classpathSeparator();
};

} // namespace lami
