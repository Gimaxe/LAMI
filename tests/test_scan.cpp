// Test du scanner de dossier de mods : construit les références (nom+sha256+taille)
// à partir de fichiers .jar locaux. Déterministe, hors-ligne → headless.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "core/ModScanner.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;
static void check(bool ok, const QString &l) { out<<(ok?"  [OK] ":"  [ÉCHEC] ")<<l<<"\n"; if(!ok)++failures; }

static void writeFile(const QString &path, const QByteArray &c)
{ QFile f(path); f.open(QIODevice::WriteOnly); f.write(c); }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString dir = QDir::temp().filePath("lami_scan_test");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);

    const QByteArray cB = "MOD B CONTENT";
    const QByteArray cA = "MOD A — un peu plus long";
    writeFile(QDir(dir).filePath("sodium.jar"), cB);
    writeFile(QDir(dir).filePath("lithium.jar"), cA);
    writeFile(QDir(dir).filePath("notes.txt"), "pas un mod");  // ignoré

    const QVector<ModEntry> mods = scanModsFolder(dir);

    out << "Mods détectés : " << mods.size() << "\n";
    for (const ModEntry &m : mods)
        out << "  * " << m.file << "  " << m.size << " o  sha256=" << m.sha256.left(12) << "…\n";

    check(mods.size() == 2, "2 .jar détectés (le .txt est ignoré)");
    check(mods.size() == 2 && mods[0].file == "lithium.jar", "tri par nom (lithium avant sodium)");
    check(mods.size() == 2 && mods[1].file == "sodium.jar", "sodium en second");

    const QString expectA = QString::fromLatin1(
        QCryptographicHash::hash(cA, QCryptographicHash::Sha256).toHex());
    check(!mods.isEmpty() && mods[0].sha256 == expectA, "sha256 de lithium correct");
    check(!mods.isEmpty() && mods[0].size == cA.size(), "taille de lithium correcte");

    out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                          : QString("\n%1 échec(s) ❌\n").arg(failures));
    out.flush();
    return failures == 0 ? 0 : 1;
}
