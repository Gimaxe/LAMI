// Test du module Fabric : conversion Maven→chemin (offline) + résolution du
// profil Fabric réel pour 1.20.1 (réseau, sans compte) → headless.

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include "minecraft/FabricMeta.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;
static void check(bool ok, const QString &l) { out<<(ok?"  [OK] ":"  [ÉCHEC] ")<<l<<"\n"; if(!ok)++failures; }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // --- Offline : conversion de coordonnées Maven ---
    out << "Conversion Maven → chemin :\n";
    check(mavenCoordToPath("net.fabricmc:fabric-loader:0.15.11")
              == "net/fabricmc/fabric-loader/0.15.11/fabric-loader-0.15.11.jar",
          "coordonnée simple");
    check(mavenCoordToPath("org.ow2.asm:asm:9.6")
              == "org/ow2/asm/asm/9.6/asm-9.6.jar", "groupe multi-segments");
    check(mavenCoordToPath("a:b:1.0:natives")
              == "a/b/1.0/b-1.0-natives.jar", "avec classifier");

    // --- Réseau : profils réels Fabric puis Quilt pour 1.20.1 ---
    auto *meta = new FabricMeta(&app);
    static bool fabricDone = false;

    auto checkProfile = [&](const FabricProfile &p, const QString &loaderName,
                            const QString &loaderLibNeedle) {
        out << "\nProfil " << loaderName << " résolu :\n";
        out << "  mainClass : " << p.mainClass << "\n";
        out << "  libraries : " << p.libraries.size() << "\n";
        if (!p.libraries.isEmpty())
            out << "  ex. lib   : " << p.libraries.first().name << "\n";

        check(p.valid, loaderName + " : profil valide");
        check(p.mainClass.contains("Knot"), loaderName + " : mainClass = KnotClient");
        check(p.libraries.size() >= 3, loaderName + " : au moins 3 libraries");
        bool hasLoader = false, allHaveUrl = true;
        for (const Library &l : p.libraries) {
            if (l.name.contains(loaderLibNeedle)) hasLoader = true;
            if (l.url.isEmpty() || l.path.isEmpty()) allHaveUrl = false;
        }
        check(hasLoader, loaderName + " : loader présent (" + loaderLibNeedle + ")");
        check(allHaveUrl, loaderName + " : toutes les libs ont url + chemin");
    };

    QObject::connect(meta, &FabricMeta::resolved, &app, [&](const FabricProfile &p) {
        if (!fabricDone) {
            checkProfile(p, "Fabric", "fabric-loader");
            fabricDone = true;
            out << "\nRésolution du profil Quilt pour 1.20.1…\n"; out.flush();
            meta->resolve("1.20.1", QString(), FabricMeta::quiltBase());
        } else {
            checkProfile(p, "Quilt", "quilt-loader");
            out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                                  : QString("\n%1 échec(s) ❌\n").arg(failures));
            out.flush();
            app.exit(failures == 0 ? 0 : 1);
        }
    });

    QObject::connect(meta, &FabricMeta::errorOccurred, &app, [&](const QString &m) {
        out << "\n❌ " << m << "\n"; out.flush(); app.exit(2);
    });

    QTimer::singleShot(25000, &app, [&]{ out << "\nTimeout.\n"; app.exit(3); });

    out << "\nRésolution du profil Fabric pour 1.20.1…\n"; out.flush();
    meta->resolve("1.20.1");   // Fabric par défaut, dernier loader
    return app.exec();
}
