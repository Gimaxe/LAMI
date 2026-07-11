// Test du provisionnement Java : télécharge le JRE Mojang (java-runtime-gamma =
// Java 17, requis par MC 1.20.1) puis exécute `java -version` pour vérifier.
// Réseau (~40 Mo) mais AUCUN compte → headless.

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include "minecraft/JavaProvisioner.h"

using namespace lami;

static QTextStream out(stdout);

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString dataRoot = QDir::temp().filePath("lami_java_test");
    auto *jp = new JavaProvisioner(dataRoot, &app);

    QObject::connect(jp, &JavaProvisioner::progress, &app, [&](const QString &s) {
        out << "  … " << s << "\r"; out.flush();
    });
    QObject::connect(jp, &JavaProvisioner::errorOccurred, &app, [&](const QString &e) {
        out << "\n❌ " << e << "\n"; app.exit(1);
    });
    QObject::connect(jp, &JavaProvisioner::ready, &app, [&](const QString &javaPath) {
        out << "\n✅ Java provisionné : " << javaPath << "\n";

        // Exécute java -version pour confirmer la version.
        auto *proc = new QProcess(&app);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         &app, [&, proc](int, QProcess::ExitStatus) {
            const QString v = QString::fromUtf8(proc->readAll());
            out << v;
            const bool ok = v.contains("17.") || v.contains("version \"17");
            out << (ok ? "  ✅ C'est bien Java 17\n" : "  ⚠️ version inattendue\n");
            app.exit(ok ? 0 : 2);
        });
        proc->start(javaPath, {"-version"});
    });

    QTimer::singleShot(120000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });

    out << "Provisionnement de java-runtime-gamma (Java 17) pour " << JavaProvisioner::osKey() << "…\n";
    out.flush();
    jp->provision("java-runtime-gamma");
    return app.exec();
}
