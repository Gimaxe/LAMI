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

QString modLocalPath(const ModEntry &mod)
{
    return "mods/" + mod.file;
}

QString modBankPath(const ServerInfo &server, const ModEntry &mod)
{
    return QStringLiteral("mods/%1/%2/%3")
        .arg(server.minecraftVersion, server.loader, mod.file);
}

QJsonObject serverToJson(const ServerInfo &s)
{
    QJsonArray mods;
    for (const ModEntry &m : s.mods) {
        mods.append(QJsonObject{
            {"file", m.file},
            {"sha256", m.sha256},
            {"size", static_cast<double>(m.size)},
        });
    }
    return QJsonObject{
        {"id", s.id},
        {"name", s.name},
        {"address", s.address},
        {"minecraft_version", s.minecraftVersion},
        {"loader", s.loader},
        {"loader_version", s.loaderVersion},
        {"mods", mods},
    };
}

} // namespace lami
