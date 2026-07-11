#include "roles/Permissions.h"

namespace lami {

QString RoleResolver::normalize(const QString &uuid)
{
    return QString(uuid).remove('-').toLower();
}

Role RoleResolver::roleFor(const QString &uuid, const RoleTable &roles)
{
    const QString target = normalize(uuid);
    if (target.isEmpty())
        return Role::Player;

    // Lookup direct d'abord (cas fréquent : clé identique).
    if (roles.contains(uuid))
        return roles.value(uuid);

    // Sinon comparaison normalisée (tirets/casse).
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        if (normalize(it.key()) == target)
            return it.value();
    }
    return Role::Player;
}

bool RoleResolver::can(Role role, Capability cap)
{
    switch (cap) {
    case Capability::JoinServer:
        return true;  // tout le monde peut rejoindre
    case Capability::PublishServer:
        return role == Role::Host || role == Role::SuperAdmin;
    case Capability::ManageRoles:
    case Capability::Supervise:
        return role == Role::SuperAdmin;
    }
    return false;
}

bool RoleResolver::can(const QString &uuid, const RoleTable &roles, Capability cap)
{
    return can(roleFor(uuid, roles), cap);
}

QString RoleResolver::capabilityName(Capability cap)
{
    switch (cap) {
    case Capability::JoinServer:    return "JoinServer";
    case Capability::PublishServer: return "PublishServer";
    case Capability::ManageRoles:   return "ManageRoles";
    case Capability::Supervise:     return "Supervise";
    }
    return "?";
}

} // namespace lami
