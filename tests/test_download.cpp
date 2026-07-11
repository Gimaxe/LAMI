// Test du téléchargeur : résout 1.20.1, télécharge ses 4 plus petites libraries
// (vraies URLs Mojang) avec vérif SHA1, puis relance pour prouver le "skip".
// Réseau, sans compte → headless.

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

#include "minecraft/Downloader.h"
#include "minecraft/MojangMeta.h"

using namespace lami;

static QTextStream out(stdout);

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString dir = QDir::temp().filePath("lami_dl_test");
    QDir(dir).removeRecursively();

    auto *meta = new MojangMeta(&app);
    auto *dl   = new Downloader(4, &app);

    // Construit les tâches à partir des 4 plus petites libs.
    auto buildTasks = [&](const VersionInfo &v) {
        QVector<Library> libs = v.libraries;
        std::sort(libs.begin(), libs.end(),
                  [](const Library &a, const Library &b) { return a.size < b.size; });
        QVector<DownloadTask> tasks;
        for (int i = 0; i < libs.size() && i < 4; ++i) {
            DownloadTask t;
            t.url  = libs[i].url;
            t.dest = QDir(dir).filePath("libraries/" + libs[i].path);
            t.expectedHash = libs[i].sha1;   // algo SHA1 par défaut
            t.size = libs[i].size;
            tasks.push_back(t);
            out << "  · " << libs[i].name << " (" << libs[i].size / 1024 << " Ko)\n";
        }
        return tasks;
    };

    static QVector<DownloadTask> tasks;
    static bool secondPass = false;

    QObject::connect(dl, &Downloader::fileFailed, &app, [&](const QString &d, const QString &r) {
        out << "  ❌ " << QFileInfo(d).fileName() << " : " << r << "\n"; out.flush();
    });

    QObject::connect(dl, &Downloader::finished, &app, [&](int ok, int failed) {
        out << (secondPass ? "\n[2e passe] " : "\n[1re passe] ")
            << ok << " ok, " << failed << " échec(s)\n";
        out.flush();

        if (!secondPass) {
            // Vérif disque après la 1re passe.
            bool allThere = true;
            for (const DownloadTask &t : tasks)
                if (!QFileInfo::exists(t.dest)) allThere = false;
            out << (allThere ? "  ✅ tous les fichiers présents sur le disque\n"
                             : "  ❌ fichiers manquants\n");
            out << "  → relance pour tester le skip des fichiers déjà valides…\n";
            out.flush();
            secondPass = true;
            dl->start(tasks);  // doit tout sauter (aucun réseau)
        } else {
            const bool skipOk = (ok == tasks.size() && failed == 0);
            out << (skipOk ? "  ✅ 2e passe : tout sauté (déjà valide) — reprise OK\n"
                           : "  ❌ 2e passe inattendue\n");
            out.flush();
            app.exit(failed == 0 && skipOk ? 0 : 1);
        }
    });

    QObject::connect(meta, &MojangMeta::resolved, &app, [&](const VersionInfo &v) {
        out << "Version " << v.id << " résolue. Libs les plus petites :\n";
        tasks = buildTasks(v);
        out << "Téléchargement…\n"; out.flush();
        dl->start(tasks);
    });
    QObject::connect(meta, &MojangMeta::errorOccurred, &app, [&](const QString &m) {
        out << "❌ " << m << "\n"; out.flush(); app.exit(2);
    });

    QTimer::singleShot(40000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });

    out << "Résolution de 1.20.1…\n"; out.flush();
    meta->resolve("1.20.1");
    return app.exec();
}
