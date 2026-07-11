// Test de la publication avec BANQUE de mods : refus si rôle insuffisant, puis
// publication réelle (upload d'un mod de test dans mods/<v>/<loader>/ + manifeste),
// relecture, et nettoyage (manifeste + fichier de banque). Repo laissé propre.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "core/Publisher.h"
#include "github/GitHubClient.h"

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

    auto *ghPub    = new GitHubClient("Gimaxe", "LAMI-db", "main", &app);
    auto *ghVerify = new GitHubClient("Gimaxe", "LAMI-db", "main", &app);
    ghPub->setToken(token);
    ghVerify->setToken(token);
    auto *pub = new Publisher(ghPub, &app);

    const QString GIMAXE  = "6ce55042-b808-45c4-999b-54c99cd96398";
    const QString UNKNOWN = "00000000-0000-0000-0000-000000000000";
    const QString TEST_ID = "lami-test-publish";

    // Mod de test créé localement.
    const QString modsDir = QDir::temp().filePath("lami_publish_mods");
    QDir(modsDir).removeRecursively();
    QDir().mkpath(modsDir);
    const QByteArray modBytes = "FAKE MOD CONTENT LAMI TEST";
    { QFile mf(QDir(modsDir).filePath("lami-test-mod.jar"));
      mf.open(QIODevice::WriteOnly); mf.write(modBytes); }

    ModEntry mod;
    mod.file = "lami-test-mod.jar";
    mod.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(modBytes, QCryptographicHash::Sha256).toHex());
    mod.size = modBytes.size();

    const QString TEST_ADDR = "test.lami.invalid";
    ServerInfo srv;
    srv.id = TEST_ID; srv.name = "TEST auto LAMI (supprime-moi)";
    srv.address = TEST_ADDR;
    srv.minecraftVersion = "1.20.1"; srv.loader = "fabric"; srv.loaderVersion = "0.15.11";
    srv.mods = { mod }; srv.valid = true;

    const QString bankPath = modBankPath(srv, mod);  // mods/1.20.1/fabric/lami-test-mod.jar
    // Contenu original de l'index, à restaurer au nettoyage.
    const QByteArray origIndex = "{\n    \"play.atraxe.fr\": \"exemple\"\n}\n";

    auto finish = [&]() {
        out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                              : QString("\n%1 échec(s) ❌\n").arg(failures));
        out.flush();
        app.exit(failures == 0 ? 0 : 1);
    };

    // Nettoyage séquencé : manifeste → banque → restauration de l'index.
    QObject::connect(ghVerify, &GitHubClient::fileDeleted, &app, [&](const QString &p) {
        out << "  … supprimé : " << p << "\n"; out.flush();
        if (p.startsWith("servers/")) {
            ghVerify->deleteFile(bankPath, "Nettoyage test banque LAMI");
        } else {
            out << "  … restauration de l'index…\n"; out.flush();
            ghVerify->putFile("servers/index.json", origIndex, "Restauration index (test LAMI)");
        }
    });

    // Index restauré → fin.
    QObject::connect(ghVerify, &GitHubClient::filePut, &app, [&](const QString &) {
        check(true, "repo laissé propre (manifeste, banque, index restauré)");
        finish();
    });

    // Résolution PAR ADRESSE → prouve manifeste + index d'un coup.
    QObject::connect(ghVerify, &GitHubClient::serverFetched, &app, [&](const ServerInfo &s) {
        check(s.id == TEST_ID, "serveur résolu PAR ADRESSE (index) → bon serveur");
        check(s.mods.size() == 1 && s.mods.first().file == "lami-test-mod.jar",
              "mod correctement référencé dans le manifeste");
        out << "  … nettoyage (manifeste, banque, index)…\n"; out.flush();
        ghVerify->deleteFile("servers/" + TEST_ID + ".json", "Nettoyage test publication LAMI");
    });

    QObject::connect(pub, &Publisher::progress, &app, [&](const QString &s) {
        out << "  … " << s << "\n"; out.flush();
    });

    QObject::connect(pub, &Publisher::published, &app, [&](const QString &id) {
        check(id == TEST_ID, "publication signalée");
        out << "  … résolution par adresse (après propagation)…\n"; out.flush();
        // Petit délai : lecture juste après écriture peut subir une latence GitHub.
        QTimer::singleShot(2500, &app, [&]{ ghVerify->fetchServerByAddress(TEST_ADDR); });
    });

    QObject::connect(pub, &Publisher::failed, &app, [&](const QString &m) {
        out << "\n❌ échec technique : " << m << "\n"; out.flush(); app.exit(2);
    });
    QObject::connect(ghVerify, &GitHubClient::errorOccurred, &app, [&](const QString &m) {
        out << "\n❌ vérif : " << m << "\n"; out.flush(); app.exit(2);
    });
    QObject::connect(ghVerify, &GitHubClient::writeError, &app, [&](const QString &m) {
        out << "\n❌ nettoyage : " << m << "\n"; out.flush(); app.exit(2);
    });

    bool deniedSeen = false;
    QObject::connect(pub, &Publisher::denied, &app, [&](const QString &reason) {
        if (!deniedSeen) {
            deniedSeen = true;
            check(true, "publication refusée pour UUID inconnu (Player)");
            out << "    (raison: " << reason << ")\n";
            out << "\n→ Publication réelle en tant que gimaxe (superadmin)…\n"; out.flush();
            pub->publish(srv, modsDir, GIMAXE);
        } else {
            check(false, "refus inattendu pour gimaxe"); finish();
        }
    });

    QTimer::singleShot(40000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });

    out << "Test 1 — publication avec un rôle insuffisant :\n"; out.flush();
    pub->publish(srv, modsDir, UNKNOWN);
    return app.exec();
}
