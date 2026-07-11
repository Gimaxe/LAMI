#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lami {

// Une bibliothèque Java requise par une version de Minecraft (artifact).
struct Library {
    QString name;   // ex. "com.google.guava:guava:31.1-jre"
    QString path;   // chemin relatif dans libraries/, ex. "com/google/guava/..."
    QString url;    // URL de téléchargement
    QString sha1;
    qint64  size = 0;
};

// Métadonnées d'une version de Minecraft, extraites du JSON Mojang.
// Suffisant pour préparer le téléchargement et la ligne de lancement.
struct VersionInfo {
    QString id;              // "1.20.1"
    QString type;            // "release" / "snapshot"
    QString mainClass;       // classe d'entrée de la JVM

    QString assetIndexId;    // ex. "5"
    QString assetIndexUrl;   // JSON listant les assets
    QString assetIndexSha1;

    QString javaComponent;   // runtime Java Mojang requis, ex. "java-runtime-gamma"
    int     javaMajor = 8;   // version majeure de Java (8, 17, 21…)

    QString clientUrl;       // client.jar
    QString clientSha1;
    qint64  clientSize = 0;

    QVector<Library> libraries;  // déjà filtrées pour l'OS courant

    // Gabarits d'arguments (avec placeholders ${...}), déjà filtrés pour l'OS.
    QStringList jvmArgs;   // ex. "-cp", "${classpath}", "-Djava.library.path=${natives_directory}"
    QStringList gameArgs;  // ex. "--username", "${auth_player_name}", ...

    bool valid = false;
};

// Nom d'OS au sens des "rules" Mojang : "windows" | "osx" | "linux".
QString currentOsName();

} // namespace lami
