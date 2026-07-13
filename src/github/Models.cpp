#include "github/Models.h"

#include <QJsonArray>

namespace lami {

Role roleFromString(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == "superadmin" || v == "super_admin" || v == "admin")
        return Role::SuperAdmin;
    if (v == "host" || v == "hebergeur" || v == "hébergeur")
        return Role::Host;
    return Role::Player;
}

QString roleToString(Role role)
{
    switch (role) {
    case Role::SuperAdmin: return "superadmin";
    case Role::Host:       return "host";
    case Role::Player:     return "player";
    }
    return "player";
}

const QVector<ModEntry> &ServerInfo::assetList(const QString &type) const
{
    if (type == assets::Plugins)       return plugins;
    if (type == assets::ResourcePacks) return resourcePacks;
    if (type == assets::Shaders)       return shaders;
    return mods;
}
QVector<ModEntry> &ServerInfo::assetList(const QString &type)
{
    if (type == assets::Plugins)       return plugins;
    if (type == assets::ResourcePacks) return resourcePacks;
    if (type == assets::Shaders)       return shaders;
    return mods;
}

QString assetLocalPath(const QString &type, const ModEntry &e)
{
    // Dossier client réellement lu par le jeu : les shaders vont dans
    // « shaderpacks/ » (Iris/OptiFine), les autres portent le nom de la catégorie.
    const QString dir = (type == assets::Shaders) ? QStringLiteral("shaderpacks") : type;
    return dir + "/" + e.file;   // mods/… plugins/… resourcepacks/… shaderpacks/…
}

QString assetBankPath(const ServerInfo &server, const QString &type, const ModEntry &e)
{
    // Les mods dépendent du loader ; les autres seulement de la version.
    if (type == assets::Mods)
        return QStringLiteral("mods/%1/%2/%3").arg(server.minecraftVersion, server.loader, e.file);
    return QStringLiteral("%1/%2/%3").arg(type, server.minecraftVersion, e.file);
}

static QJsonArray assetArray(const QVector<ModEntry> &list)
{
    QJsonArray a;
    for (const ModEntry &m : list)
        a.append(QJsonObject{{"file", m.file}, {"sha256", m.sha256},
                             {"size", static_cast<double>(m.size)}});
    return a;
}

QJsonObject serverToJson(const ServerInfo &s)
{
    return QJsonObject{
        {"id", s.id},
        {"name", s.name},
        {"address", s.address},
        {"minecraft_version", s.minecraftVersion},
        {"loader", s.loader},
        {"loader_version", s.loaderVersion},
        {"mods", assetArray(s.mods)},
        {"plugins", assetArray(s.plugins)},
        {"resourcepacks", assetArray(s.resourcePacks)},
        {"shaders", assetArray(s.shaders)},
        {"owner", s.owner},   // UUID du créateur (droits de modif/suppression)
    };
}

} // namespace lami
