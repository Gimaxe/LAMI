#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "core/InstanceManager.h"
#include "ui/HomePage.h"
#include "ui/MainWindow.h"

namespace {

QString readFileTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

// Mode CLI headless : `lami --plan <serverId>` construit et affiche le plan de
// lancement d'un serveur (sans télécharger ni lancer). Pratique pour vérifier la
// chaîne bout-en-bout, y compris sur une machine sans écran.
int runPlanCli(int argc, char **argv, const QString &serverId)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString token = readFileTrimmed(QDir::homePath() + "/LAMI/.token");
    if (token.isEmpty()) {
        out << "ERREUR : ~/LAMI/.token manquant.\n";
        return 1;
    }

    const QString dataRoot = QDir::homePath() + "/.local/share/LAMI";
    auto *mgr = new lami::InstanceManager("Gimaxe", "LAMI-db", "main", token,
                                          dataRoot, "java", &app);

    // Session factice tant que l'auth Microsoft n'est pas approuvée.
    lami::MinecraftSession session;
    session.name = "gimaxe";
    session.uuid = "6ce55042b80845c4999b54c99cd96398";
    session.minecraftToken = "PENDING_AUTH";
    session.valid = true;

    QObject::connect(mgr, &lami::InstanceManager::progress, &app,
                     [&](const QString &s) { out << "  … " << s << "\n"; out.flush(); });

    QObject::connect(mgr, &lami::InstanceManager::failed, &app, [&](const QString &m) {
        out << "\n❌ " << m << "\n"; out.flush(); app.exit(1);
    });

    QObject::connect(mgr, &lami::InstanceManager::planReady, &app,
                     [&](const lami::LaunchPlan &p) {
        qint64 bytes = 0;
        for (const lami::DownloadTask &t : p.downloads) bytes += t.size;
        out << "\nPlan pour « " << p.server.name << " » (" << p.version.id << ") :\n"
            << "  À télécharger/vérifier : " << p.downloads.size()
            << " fichiers (~" << bytes / 1024 / 1024 << " Mo)\n"
            << "  mainClass : " << p.version.mainClass << "\n"
            << "  gameDir   : " << p.gameDir << "\n\n"
            << "Commande de lancement :\n  " << p.launchCommand.join(' ') << "\n";
        out.flush();
        app.quit();
    });

    out << "Construction du plan pour « " << serverId << " »…\n"; out.flush();
    QTimer::singleShot(30000, &app, [&] { out << "\nTimeout.\n"; app.exit(2); });
    mgr->plan(serverId, session);
    return app.exec();
}

} // namespace

// Capture de l'écran d'accueil avec une session factice (dev headless).
static int screenshotHome(int argc, char **argv, const QString &file)
{
    QApplication app(argc, argv);
    auto *home = new lami::HomePage();
    lami::MinecraftSession s;
    s.name = "gimaxe"; s.uuid = "6ce55042b80845c4999b54c99cd96398"; s.valid = true;
    home->setSession(s, lami::Role::Host);
    home->resize(560, 460);
    home->show();
    QTimer::singleShot(600, &app, [&]() { home->grab().save(file); app.quit(); });
    return app.exec();
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc - 1; ++i)
        if (QString::fromUtf8(argv[i]) == "--screenshot-home")
            return screenshotHome(argc, argv, QString::fromUtf8(argv[i + 1]));

    // Mode CLI : lami --plan <serverId>
    for (int i = 1; i < argc - 1; ++i) {
        if (QString::fromUtf8(argv[i]) == "--plan")
            return runPlanCli(argc, argv, QString::fromUtf8(argv[i + 1]));
    }

    // Mode capture d'écran (dev headless) : lami --screenshot <fichier.png>
    QString shot;
    for (int i = 1; i < argc - 1; ++i)
        if (QString::fromUtf8(argv[i]) == "--screenshot")
            shot = QString::fromUtf8(argv[i + 1]);

    // Mode graphique par défaut.
    QApplication app(argc, argv);
    QApplication::setApplicationName("LAMI");
    QApplication::setApplicationDisplayName("LAMI — Launcher Atraxe MInecraft");
    QApplication::setOrganizationName("Atraxe");
    QApplication::setApplicationVersion("0.1.0");

    lami::MainWindow window;
    window.show();

    if (!shot.isEmpty()) {
        // Laisse le temps au layout de se faire, capture, puis quitte.
        QTimer::singleShot(600, &app, [&]() {
            window.grab().save(shot);
            app.quit();
        });
    }

    return app.exec();
}
