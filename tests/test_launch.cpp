// Test du constructeur de commande de lancement. Déterministe, sans réseau ni
// compte : VersionInfo + MinecraftSession factices → vérifie la commande produite.

#include <QCoreApplication>
#include <QTextStream>

#include "minecraft/LaunchBuilder.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;

static void check(bool ok, const QString &label)
{
    out << (ok ? "  [OK] " : "  [ÉCHEC] ") << label << "\n";
    if (!ok) ++failures;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // --- Version factice (façon "arguments" moderne) ---
    VersionInfo v;
    v.id = "1.20.1"; v.type = "release";
    v.mainClass = "net.minecraft.client.main.Main";
    v.assetIndexId = "5";
    Library a; a.path = "com/example/foo/1.0/foo-1.0.jar"; v.libraries << a;
    Library b; b.path = "com/example/bar/2.0/bar-2.0.jar"; v.libraries << b;
    v.jvmArgs  = {"-Djava.library.path=${natives_directory}", "-cp", "${classpath}"};
    v.gameArgs = {"--username", "${auth_player_name}",
                  "--uuid", "${auth_uuid}",
                  "--accessToken", "${auth_access_token}",
                  "--version", "${version_name}",
                  "--gameDir", "${game_directory}",
                  "--assetsDir", "${assets_root}",
                  "--assetIndex", "${assets_index_name}",
                  "--userType", "${user_type}"};

    // --- Session factice ---
    MinecraftSession s;
    s.name = "gimaxe"; s.uuid = "6ce55042b80845c4999b54c99cd96398";
    s.minecraftToken = "FAKE_TOKEN_123"; s.valid = true;

    // --- Chemins factices ---
    LaunchPaths p;
    p.javaPath = "/opt/jre/bin/java";
    p.gameDir = "/instances/exemple";
    p.assetsRoot = "/shared/assets";
    p.librariesRoot = "/shared/libraries";
    p.nativesDir = "/instances/exemple/natives";
    p.clientJar = "/instances/exemple/client.jar";

    const QStringList cmd = LaunchBuilder::build(v, s, p);
    const QString sep = LaunchBuilder::classpathSeparator();

    out << "Commande générée (" << cmd.size() << " éléments) :\n";
    out << "  " << cmd.join(' ').left(300) << "…\n\n";

    check(cmd.first() == "/opt/jre/bin/java", "argv[0] = java");
    check(cmd.contains("net.minecraft.client.main.Main"), "mainClass présent");
    check(cmd.indexOf("net.minecraft.client.main.Main") > cmd.indexOf("-cp"),
          "mainClass après les args JVM");

    check(cmd.contains("gimaxe"), "pseudo substitué (--username)");
    check(cmd.contains("6ce55042b80845c4999b54c99cd96398"), "UUID substitué");
    check(cmd.contains("FAKE_TOKEN_123"), "access token substitué");
    check(cmd.contains("/instances/exemple"), "game directory substitué");
    check(cmd.contains("5"), "assetIndex substitué");

    // Classpath : contient les libs + client.jar, joints par le séparateur OS.
    const int cpIdx = cmd.indexOf("-cp") + 1;
    const QString cp = (cpIdx > 0 && cpIdx < cmd.size()) ? cmd[cpIdx] : QString();
    check(cp.contains("foo-1.0.jar") && cp.contains("bar-2.0.jar"),
          "classpath contient les libraries");
    check(cp.endsWith("client.jar"), "classpath se termine par client.jar");
    check(cp.contains(sep), QString("classpath joint par '%1'").arg(sep));

    // Le natives_directory doit être substitué dans l'arg JVM.
    check(cmd.contains("-Djava.library.path=/instances/exemple/natives"),
          "natives_directory substitué");

    // Aucun placeholder ${...} ne doit subsister.
    bool leftover = false;
    for (const QString &tok : cmd)
        if (tok.contains("${")) { leftover = true; out << "    reste: " << tok << "\n"; }
    check(!leftover, "aucun placeholder ${…} restant");

    out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                          : QString("\n%1 test(s) en échec ❌\n").arg(failures));
    out.flush();
    return failures == 0 ? 0 : 1;
}
