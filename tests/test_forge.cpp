#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "minecraft/ForgeInstaller.h"

using namespace lami;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString dataRoot = argc > 1 ? argv[1] : "/tmp/lami_forge_test";
    const QString javaPath = argc > 2 ? argv[2]
        : "/tmp/lami_java_test/java/java-runtime-gamma/bin/java";
    const QString mc     = argc > 3 ? argv[3] : "1.20.1";
    const QString loader = argc > 4 ? argv[4] : "forge";

    ForgeInstaller inst(dataRoot, javaPath);
    QObject::connect(&inst, &ForgeInstaller::progress, [](const QString &s) {
        std::printf("[..] %s\n", s.toUtf8().constData());
        std::fflush(stdout);
    });
    QObject::connect(&inst, &ForgeInstaller::resolved, [&](const ForgeProfile &p) {
        std::printf("[OK] mainClass=%s libs=%d jvmArgs=%d gameArgs=%d\n",
                    p.mainClass.toUtf8().constData(), p.libraries.size(),
                    p.jvmArgs.size(), p.gameArgs.size());
        int noUrl = 0;
        for (const Library &l : p.libraries) if (l.url.isEmpty()) noUrl++;
        std::printf("[OK] libs sans url (locales) = %d\n", noUrl);
        app.exit(0);
    });
    QObject::connect(&inst, &ForgeInstaller::errorOccurred, [&](const QString &e) {
        std::printf("[ERR] %s\n", e.toUtf8().constData());
        app.exit(1);
    });

    QTimer::singleShot(0, [&]() { inst.resolve(mc, loader); });
    QTimer::singleShot(240000, [&]() { std::printf("[ERR] timeout\n"); app.exit(2); });
    return app.exec();
}
