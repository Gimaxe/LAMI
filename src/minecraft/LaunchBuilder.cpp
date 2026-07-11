#include "minecraft/LaunchBuilder.h"

#include <QDir>
#include <QHash>

namespace lami {

QString LaunchBuilder::classpathSeparator()
{
    return currentOsName() == "windows" ? ";" : ":";
}

QStringList LaunchBuilder::classpathEntries(const VersionInfo &version,
                                            const LaunchPaths &paths)
{
    QStringList entries;
    for (const Library &lib : version.libraries)
        entries << QDir(paths.librariesRoot).filePath(lib.path);
    entries << paths.clientJar;  // le client.jar en dernier
    return entries;
}

QString LaunchBuilder::classpath(const VersionInfo &version, const LaunchPaths &paths)
{
    return classpathEntries(version, paths).join(classpathSeparator());
}

namespace {
// Remplace tous les ${clé} d'un token par leur valeur.
QString substitute(QString token, const QHash<QString, QString> &vars)
{
    for (auto it = vars.begin(); it != vars.end(); ++it)
        token.replace("${" + it.key() + "}", it.value());
    return token;
}
} // namespace

QStringList LaunchBuilder::build(const VersionInfo &version,
                                 const MinecraftSession &session,
                                 const LaunchPaths &paths,
                                 const QString &launcherName,
                                 const QString &launcherVersion)
{
    QHash<QString, QString> vars{
        {"auth_player_name", session.name},
        {"auth_uuid", session.uuid},
        {"auth_access_token", session.minecraftToken},
        {"auth_xuid", QString()},
        {"clientid", QString()},
        {"user_type", "msa"},
        {"version_name", version.id},
        {"version_type", version.type},
        {"game_directory", paths.gameDir},
        {"assets_root", paths.assetsRoot},
        {"game_assets", paths.assetsRoot},          // legacy
        {"assets_index_name", version.assetIndexId},
        {"natives_directory", paths.nativesDir},
        {"launcher_name", launcherName},
        {"launcher_version", launcherVersion},
        {"classpath", classpath(version, paths)},
    };

    QStringList argv;
    argv << paths.javaPath;
    for (const QString &t : version.jvmArgs)
        argv << substitute(t, vars);
    argv << version.mainClass;
    for (const QString &t : version.gameArgs)
        argv << substitute(t, vars);
    return argv;
}

} // namespace lami
