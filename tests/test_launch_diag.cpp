// Diagnostic : installe un serveur puis LANCE réellement la commande de jeu en
// headless, capturant stdout/stderr pour comprendre un crash au démarrage.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include "core/InstanceManager.h"
#include "minecraft/Downloader.h"

using namespace lami;
static QTextStream out(stdout);

static QString readToken() {
    QFile f(QDir::homePath() + "/LAMI/.token");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const QString token = readToken();
    if (token.isEmpty()) { out << "ERREUR: ~/LAMI/.token manquant.\n"; return 1; }

    const QString serverId = argc > 1 ? argv[1] : "neotest";
    const QString java = "/tmp/lami_java_test/java/java-runtime-gamma/bin/java";
    const QString dataRoot = QDir::temp().filePath("lami_launch_diag");

    auto *mgr = new InstanceManager("Gimaxe", "LAMI-db", "main", token, dataRoot, java, &app);
    MinecraftSession s; s.name = "gimaxe"; s.uuid = "6ce55042b80845c4999b54c99cd96398";
    s.minecraftToken = "0"; s.valid = true;

    QObject::connect(mgr, &InstanceManager::progress, &app, [&](const QString &st) {
        out << "  … " << st << "\n"; out.flush();
    });
    QObject::connect(mgr, &InstanceManager::failed, &app, [&](const QString &m) {
        out << "\n❌ plan: " << m << "\n"; app.exit(2);
    });
    QObject::connect(mgr, &InstanceManager::planReady, &app, [&](const LaunchPlan &plan) {
        out << "\nPlan prêt : " << plan.downloads.size() << " fichiers. Téléchargement…\n"; out.flush();
        auto *dl = new Downloader(8, &app);
        QObject::connect(dl, &Downloader::finished, &app, [&, plan](int ok, int failed) {
            out << "Téléchargement fini : " << ok << " ok, " << failed << " échecs.\n";
            QStringList cmd = plan.launchCommand;
            const QString prog = cmd.takeFirst();
            out << "\n=== LANCEMENT (headless, 40s max) ===\n";
            out << prog << " " << cmd.join(' ').left(400) << " …\n\n"; out.flush();
            QDir().mkpath(plan.gameDir);
            const QString logPath = "/tmp/neo_game.log";
            auto *proc = new QProcess(&app);
            proc->setWorkingDirectory(plan.gameDir);
            proc->setProcessChannelMode(QProcess::MergedChannels);
            proc->setStandardOutputFile(logPath);   // survit même à un crash natif
            QObject::connect(proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                             &app, [&, logPath](int code, QProcess::ExitStatus st) {
                out << "\n--- jeu terminé (code " << code << ", status "
                    << (st==QProcess::CrashExit?"CRASH":"normal") << "). Log: " << logPath << " ---\n";
                app.exit(0);
            });
            proc->start(prog, cmd);
            QTimer::singleShot(40000, &app, [proc]() { if (proc->state()==QProcess::Running) proc->kill(); });
        });
        dl->start(plan.downloads);
    });

    QTimer::singleShot(600000, &app, [&]{ out << "\nTimeout global.\n"; app.exit(3); });
    out << "Plan pour « " << serverId << " »…\n"; mgr->plan(serverId, s);
    return app.exec();
}
