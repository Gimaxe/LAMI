// Vérifie que le plan d'installation d'un serveur Forge inclut bien les mods du
// repo (téléchargement authentifié). Passe par l'installeur Forge réel.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "core/InstanceManager.h"

using namespace lami;

static QTextStream out(stdout);

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

    const QString serverId = argc > 1 ? argv[1] : "test";
    const QString java = "/tmp/lami_java_test/java/java-runtime-gamma/bin/java";
    const QString dataRoot = QDir::temp().filePath("lami_install_forge_test");

    auto *mgr = new InstanceManager("Gimaxe", "LAMI-db", "main", token, dataRoot, java, &app);

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
        int mods = 0; const DownloadTask *modTask = nullptr;
        for (const DownloadTask &t : p.downloads)
            if (t.dest.contains("/instances/") && t.dest.contains("/mods/")) { ++mods; modTask = &t; }

        out << "\nServeur « " << p.server.name << " » loader=" << p.server.loader
            << " mods_manifeste=" << p.server.mods.size()
            << " mods_a_DL=" << mods << "\n";
        if (modTask) {
            bool auth = false;
            for (const auto &h : modTask->headers) if (h.first == "Authorization") auth = true;
            out << "  modTask url=" << modTask->url << "\n"
                << "  dest=" << modTask->dest << "\n"
                << "  auth=" << (auth ? "OUI" : "NON") << "\n";
        }
        out << "  mainClass=" << p.version.mainClass << "\n";
        out.flush();
        app.exit(mods >= p.server.mods.size() ? 0 : 4);
    });

    QTimer::singleShot(240000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });
    out << "Construction du plan pour « " << serverId << " »…\n"; out.flush();
    mgr->plan(serverId, s);
    return app.exec();
}
