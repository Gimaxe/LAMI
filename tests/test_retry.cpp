#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <QtGlobal>
#include <cstdio>
#include "minecraft/Downloader.h"
using namespace lami;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString dir = QDir::tempPath() + "/lami-retry-test";
    QDir(dir).removeRecursively(); QDir().mkpath(dir);
    QVector<DownloadTask> tasks;
    DownloadTask a; a.url = "http://127.0.0.1:8899/flaky"; a.dest = dir + "/flaky.txt"; tasks << a;
    DownloadTask b; b.url = "http://127.0.0.1:8899/always-bad"; b.dest = dir + "/bad.txt"; tasks << b;
    auto *dl = new Downloader(2, &app);
    QObject::connect(dl, &Downloader::finished, [&](int ok, int failed) {
        const bool flakyOk = QFile::exists(dir + "/flaky.txt");
        std::printf(flakyOk ? "[OK]  fichier instable récupéré après nouvelles tentatives\n"
                            : "[FAIL] fichier instable perdu\n");
        std::printf(ok == 1 && failed == 1 ? "[OK]  bilan correct (1 réussi, 1 en échec définitif)\n"
                                           : "[FAIL] bilan : ok=%d failed=%d\n", ok, failed);
        app.exit((flakyOk && ok == 1 && failed == 1) ? 0 : 1);
    });
    dl->start(tasks);
    QTimer::singleShot(30000, [&]{ std::printf("[FAIL] délai dépassé\n"); app.exit(2); });
    return app.exec();
}
