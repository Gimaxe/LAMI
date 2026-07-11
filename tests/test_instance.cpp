// Test d'intégration de l'orchestrateur : construit le LaunchPlan complet pour
// le serveur "exemple" du repo privé (github → Mojang → assets → sync mods →
// commande). Petits appels réseau, AUCUN gros téléchargement → headless.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "core/InstanceManager.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;
static void check(bool ok, const QString &l) { out << (ok?"  [OK] ":"  [ÉCHEC] ")<<l<<"\n"; if(!ok)++failures; }

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

    const QString dataRoot = QDir::temp().filePath("lami_instance_test");
    QDir(dataRoot).removeRecursively();

    auto *mgr = new InstanceManager("Gimaxe", "LAMI-db", "main", token,
                                    dataRoot, "/usr/bin/java", &app);

    // Session factice (l'auth réelle attend l'approbation Microsoft).
    MinecraftSession s;
    s.name = "gimaxe"; s.uuid = "6ce55042b80845c4999b54c99cd96398";
    s.minecraftToken = "FAKE_TOKEN"; s.valid = true;

    QObject::connect(mgr, &InstanceManager::progress, &app, [&](const QString &st) {
        out << "  … " << st << "\n"; out.flush();
    });

    QObject::connect(mgr, &InstanceManager::failed, &app, [&](const QString &m) {
        out << "\n❌ " << m << "\n"; out.flush(); app.exit(2);
    });

    QObject::connect(mgr, &InstanceManager::planReady, &app, [&](const LaunchPlan &p) {
        // Compte par catégorie via le chemin de destination.
        int libs = 0, assets = 0, mods = 0, client = 0, idx = 0;
        const DownloadTask *modTask = nullptr;
        for (const DownloadTask &t : p.downloads) {
            if (t.dest.contains("/libraries/")) ++libs;
            else if (t.dest.contains("/assets/objects/")) ++assets;
            else if (t.dest.contains("/assets/indexes/")) ++idx;
            else if (t.dest.contains("/instances/") && t.dest.contains("/mods/")) { ++mods; modTask = &t; }
            else if (t.dest.endsWith(".jar")) ++client;
        }

        out << "\nPlan pour « " << p.server.name << " » :\n";
        out << "  version    : " << p.version.id << "\n";
        out << "  total DL   : " << p.downloads.size()
            << " (client=" << client << ", libs=" << libs
            << ", index=" << idx << ", assets=" << assets << ", mods=" << mods << ")\n";
        out << "  cmd (" << p.launchCommand.size() << " args) : "
            << p.launchCommand.mid(0, 3).join(' ') << " …\n\n";
        out.flush();

        check(p.valid, "plan valide");
        check(p.version.id == "1.20.1", "version 1.20.1 résolue via le serveur");
        check(client == 1, "1 client.jar");
        check(libs >= 40, "libraries présentes (>=40)");
        check(idx == 1, "1 index d'assets");
        check(assets > 300, "assets listés (>300 objets)");
        check(mods == 1, "1 mod du repo à télécharger (exemple-mod.jar)");
        if (modTask) {
            check(modTask->algo == QCryptographicHash::Sha256, "mod vérifié en SHA256");
            bool hasAuth = false;
            for (const auto &h : modTask->headers)
                if (h.first == "Authorization") hasAuth = true;
            check(hasAuth, "mod téléchargé avec en-tête Authorization (repo privé)");
        }
        check(p.launchCommand.first() == "/usr/bin/java", "commande démarre par java");
        check(p.launchCommand.contains("gimaxe"), "pseudo dans la commande");
        check(p.launchCommand.contains("1.20.1"), "version dans la commande");
        check(p.gameDir.contains("instances/exemple"), "gameDir isolé du serveur");

        out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                              : QString("\n%1 échec(s) ❌\n").arg(failures));
        out.flush();
        app.exit(failures == 0 ? 0 : 1);
    });

    QTimer::singleShot(25000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });

    out << "Construction du plan pour « exemple »…\n"; out.flush();
    mgr->plan("exemple", s);
    return app.exec();
}
