// Test admin : accorde le rôle Hébergeur à un UUID de test dans roles.json,
// vérifie, puis le révoque et re-vérifie (repo laissé propre). Réseau → headless.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "github/GitHubClient.h"
#include "roles/Permissions.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;
static void check(bool ok, const QString &l) { out<<(ok?"  [OK] ":"  [ÉCHEC] ")<<l<<"\n"; if(!ok)++failures; }

static QString readToken()
{
    QFile f(QDir::homePath() + "/LAMI/.token");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString token = readToken();
    if (token.isEmpty()) { out << "ERREUR: ~/LAMI/.token manquant.\n"; return 1; }

    auto *gh = new GitHubClient("Gimaxe", "LAMI-db", "main", &app);
    gh->setToken(token);

    const QString TEST_UUID = "11111111-2222-3333-4444-555555555555";
    int phase = 0;  // 0=grant,1=verify grant,2=revoke,3=verify revoke

    auto finish = [&]() {
        out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                              : QString("\n%1 échec(s) ❌\n").arg(failures));
        out.flush();
        app.exit(failures == 0 ? 0 : 1);
    };

    QObject::connect(gh, &GitHubClient::rolesUpdated, &app, [&]() {
        if (phase == 0) {
            out << "  rôle accordé, vérification…\n"; out.flush();
            phase = 1;
            QTimer::singleShot(2000, &app, [&]{ gh->fetchRoles(); });
        } else if (phase == 2) {
            out << "  rôle révoqué, vérification…\n"; out.flush();
            phase = 3;
            QTimer::singleShot(2000, &app, [&]{ gh->fetchRoles(); });
        }
    });

    QObject::connect(gh, &GitHubClient::rolesFetched, &app, [&](const RoleTable &roles) {
        if (phase == 1) {
            check(RoleResolver::roleFor(TEST_UUID, roles) == Role::Host,
                  "UUID de test a bien le rôle Hébergeur");
            out << "  révocation…\n"; out.flush();
            phase = 2;
            gh->removeRole(TEST_UUID, "Nettoyage test admin LAMI");
        } else if (phase == 3) {
            check(RoleResolver::roleFor(TEST_UUID, roles) == Role::Player,
                  "UUID de test révoqué (retour à Joueur) — repo propre");
            finish();
        }
    });

    QObject::connect(gh, &GitHubClient::writeError, &app, [&](const QString &m) {
        out << "\n❌ écriture : " << m << "\n"; app.exit(2);
    });
    QObject::connect(gh, &GitHubClient::errorOccurred, &app, [&](const QString &m) {
        out << "\n❌ lecture : " << m << "\n"; app.exit(2);
    });

    QTimer::singleShot(40000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });

    out << "Attribution du rôle Hébergeur à un UUID de test…\n"; out.flush();
    gh->setRole(TEST_UUID, "host", "Test admin LAMI (attribution)");
    return app.exec();
}
