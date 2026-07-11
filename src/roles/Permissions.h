#pragma once

#include <QString>

#include "github/Models.h"

namespace lami {

// Actions sensibles gouvernées par le rôle (voir src/roles/README.md).
enum class Capability {
    JoinServer,     // rejoindre/jouer         — tous
    PublishServer,  // publier/màj un serveur  — Hébergeur + Super Admin
    ManageRoles,    // attribuer/retirer rôles — Super Admin
    Supervise,      // superviser l'activité   — Super Admin
};

// Résolution du rôle d'un joueur (via son UUID Minecraft) et vérification des
// permissions. Pur / sans état → entièrement testable hors-ligne.
//
// ⚠️ Ce module dit "ce rôle a-t-il le droit". Il ne PROUVE pas l'identité :
// avant une action sensible, l'UUID doit être re-vérifié via src/auth
// (jeton de session réel) — voir la note anti-usurpation du README.
class RoleResolver
{
public:
    // Rôle associé à un UUID d'après la table. UUID absent → Player.
    // Comparaison tolérante aux tirets et à la casse (les UUID Minecraft
    // apparaissent avec ou sans tirets selon les API).
    static Role roleFor(const QString &uuid, const RoleTable &roles);

    // Ce rôle possède-t-il cette capacité ?
    static bool can(Role role, Capability cap);

    // Raccourci : l'UUID (via la table) possède-t-il cette capacité ?
    static bool can(const QString &uuid, const RoleTable &roles, Capability cap);

    static QString capabilityName(Capability cap);

private:
    // minuscules + suppression des tirets, pour comparer deux UUID.
    static QString normalize(const QString &uuid);
};

} // namespace lami
