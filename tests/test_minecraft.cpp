// Test du module minecraft : résout une version réelle depuis Mojang et affiche
// ses métadonnées. Réseau mais AUCUN compte requis → OK en headless.

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include "minecraft/MojangMeta.h"

using namespace lami;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString version = (argc > 1) ? QString::fromUtf8(argv[1]) : QStringLiteral("1.20.1");

    auto *meta = new MojangMeta(&app);

    QObject::connect(meta, &MojangMeta::resolved, &app, [&](const VersionInfo &v) {
        qint64 totalLibBytes = 0;
        for (const Library &l : v.libraries) totalLibBytes += l.size;

        out << "\n✅ Version résolue (OS = " << currentOsName() << ") :\n";
        out << "  id           : " << v.id << "  (" << v.type << ")\n";
        out << "  mainClass    : " << v.mainClass << "\n";
        out << "  assetIndex   : " << v.assetIndexId << "\n";
        out << "  client.jar   : " << v.clientSize / 1024 / 1024 << " Mo  sha1="
            << v.clientSha1.left(12) << "…\n";
        out << "  libraries    : " << v.libraries.size() << " (filtrées "
            << currentOsName() << ", ~" << totalLibBytes / 1024 / 1024 << " Mo)\n";
        out << "  args JVM/jeu : " << v.jvmArgs.size() << " / " << v.gameArgs.size()
            << " gabarits\n";
        if (!v.libraries.isEmpty()) {
            out << "  ex. lib      : " << v.libraries.first().name << "\n";
            out << "                 " << v.libraries.first().path << "\n";
        }
        out.flush();
        app.quit();
    });

    QObject::connect(meta, &MojangMeta::errorOccurred, &app, [&](const QString &msg) {
        out << "\n❌ " << msg << "\n"; out.flush();
        app.exit(1);
    });

    QTimer::singleShot(20000, &app, [&]{ out << "\nTimeout.\n"; app.exit(2); });

    out << "Résolution de Minecraft " << version << " depuis Mojang…\n"; out.flush();
    meta->resolve(version);
    return app.exec();
}
