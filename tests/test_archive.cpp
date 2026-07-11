// Test de l'extracteur ZIP (miniz vendorisé) : crée un zip (2 .jar + 1 .txt +
// un .jar dans un sous-dossier), l'extrait, vérifie que seuls les .jar sortent,
// aplatis, avec le bon contenu. Hors-ligne → headless.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "core/ModArchive.h"
#include "miniz.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;
static void check(bool ok, const QString &l) { out<<(ok?"  [OK] ":"  [ÉCHEC] ")<<l<<"\n"; if(!ok)++failures; }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString dir = QDir::temp().filePath("lami_archive_test");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString zipPath = QDir(dir).filePath("mods.zip");

    // Construit un zip de test avec miniz.
    const QByteArray aData = "CONTENU DU MOD A";
    const QByteArray bData = "mod B, un peu plus long...";
    {
        mz_zip_archive z; memset(&z, 0, sizeof(z));
        mz_zip_writer_init_file(&z, zipPath.toUtf8().constData(), 0);
        mz_zip_writer_add_mem(&z, "alpha.jar", aData.constData(), aData.size(), MZ_BEST_COMPRESSION);
        mz_zip_writer_add_mem(&z, "sub/beta.jar", bData.constData(), bData.size(), MZ_BEST_COMPRESSION);
        mz_zip_writer_add_mem(&z, "readme.txt", "pas un mod", 10, MZ_BEST_COMPRESSION);
        mz_zip_writer_finalize_archive(&z);
        mz_zip_writer_end(&z);
    }
    out << "Zip de test créé (" << QFileInfo(zipPath).size() << " o).\n";

    const QString dest = QDir(dir).filePath("out");
    QString err;
    const QStringList jars = ModArchive::extractJars(zipPath, dest, &err);

    out << "Extraits : " << jars.join(", ") << (err.isEmpty() ? "" : (" / err=" + err)) << "\n";

    check(jars.size() == 2, "2 .jar extraits (le .txt ignoré)");
    check(jars.contains("alpha.jar"), "alpha.jar extrait");
    check(jars.contains("beta.jar"), "beta.jar extrait (aplati depuis sub/)");
    check(!jars.contains("readme.txt"), "readme.txt bien ignoré");

    QFile fa(QDir(dest).filePath("alpha.jar"));
    check(fa.open(QIODevice::ReadOnly) && fa.readAll() == aData, "contenu de alpha.jar intact");
    check(QFile::exists(QDir(dest).filePath("beta.jar")), "beta.jar présent sur le disque");

    out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                          : QString("\n%1 échec(s) ❌\n").arg(failures));
    out.flush();
    return failures == 0 ? 0 : 1;
}
