// Test du module roles : résolution UUID→rôle (tolérante tirets/casse) et
// matrice de permissions. 100 % local, headless.

#include <QCoreApplication>
#include <QTextStream>

#include "roles/Permissions.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;

static void check(bool ok, const QString &label)
{
    out << (ok ? "  [OK] " : "  [ÉCHEC] ") << label << "\n";
    if (!ok) ++failures;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Table de rôles façon roles.json.
    RoleTable roles;
    roles.insert("6ce55042-b808-45c4-999b-54c99cd96398", Role::SuperAdmin); // gimaxe
    roles.insert("11111111-2222-3333-4444-555555555555", Role::Host);

    const QString admin   = "6ce55042-b808-45c4-999b-54c99cd96398";
    const QString adminNoDash = "6ce55042b80845c4999b54c99cd96398";       // sans tirets
    const QString adminUpper  = "6CE55042-B808-45C4-999B-54C99CD96398";   // majuscules
    const QString host    = "11111111-2222-3333-4444-555555555555";
    const QString unknown = "00000000-0000-0000-0000-000000000000";

    out << "Résolution des rôles :\n";
    check(RoleResolver::roleFor(admin, roles) == Role::SuperAdmin, "gimaxe → SuperAdmin");
    check(RoleResolver::roleFor(adminNoDash, roles) == Role::SuperAdmin,
          "même UUID sans tirets → SuperAdmin (tolérance)");
    check(RoleResolver::roleFor(adminUpper, roles) == Role::SuperAdmin,
          "même UUID en majuscules → SuperAdmin (tolérance)");
    check(RoleResolver::roleFor(host, roles) == Role::Host, "UUID hôte → Host");
    check(RoleResolver::roleFor(unknown, roles) == Role::Player,
          "UUID inconnu → Player (défaut)");
    check(RoleResolver::roleFor("", roles) == Role::Player, "UUID vide → Player");

    out << "\nPermissions :\n";
    using C = Capability;
    // Joueur
    check(RoleResolver::can(Role::Player, C::JoinServer),        "Player peut rejoindre");
    check(!RoleResolver::can(Role::Player, C::PublishServer),    "Player NE peut PAS publier");
    check(!RoleResolver::can(Role::Player, C::ManageRoles),      "Player NE peut PAS gérer les rôles");
    // Hébergeur
    check(RoleResolver::can(Role::Host, C::JoinServer),          "Host peut rejoindre");
    check(RoleResolver::can(Role::Host, C::PublishServer),       "Host peut publier");
    check(!RoleResolver::can(Role::Host, C::ManageRoles),        "Host NE peut PAS gérer les rôles");
    // Super Admin
    check(RoleResolver::can(Role::SuperAdmin, C::PublishServer), "SuperAdmin peut publier");
    check(RoleResolver::can(Role::SuperAdmin, C::ManageRoles),   "SuperAdmin peut gérer les rôles");
    check(RoleResolver::can(Role::SuperAdmin, C::Supervise),     "SuperAdmin peut superviser");

    out << "\nRaccourci UUID→permission :\n";
    check(RoleResolver::can(admin, roles, C::ManageRoles),
          "gimaxe (UUID) peut gérer les rôles");
    check(!RoleResolver::can(unknown, roles, C::PublishServer),
          "inconnu (UUID) ne peut pas publier");

    out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                          : QString("\n%1 test(s) en échec ❌\n").arg(failures));
    out.flush();
    return failures == 0 ? 0 : 1;
}
