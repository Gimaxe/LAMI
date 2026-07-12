// Test du module sync : vérifie la synchro NON-DESTRUCTIVE sur un scénario complet.
// 100 % local (aucun réseau), donc exécutable en headless.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "sync/SyncManager.h"

using namespace lami;

static QTextStream out(stdout);
static int failures = 0;

static void check(bool ok, const QString &label)
{
    out << (ok ? "  [OK] " : "  [ÉCHEC] ") << label << "\n";
    if (!ok)
        ++failures;
}

static void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(content);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Instance de test propre.
    const QString dir = QDir::temp().filePath("lami_sync_test");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);

    SyncManager mgr(dir);

    // --- État local simulé ---
    writeFile(mgr.absPath("mods/keep_ok.jar"),        "CONTENU-A-JOUR");
    writeFile(mgr.absPath("mods/changed.jar"),        "ANCIEN-CONTENU");
    writeFile(mgr.absPath("mods/player_added.jar"),   "MOD-PERSO-DU-JOUEUR");
    writeFile(mgr.absPath("mods/removed_by_admin.jar"), "PLUS-AU-MANIFESTE");
    // (mods/missing.jar volontairement absent)

    // Registre : ce que le launcher a lui-même installé.
    mgr.saveInstalled({"mods/keep_ok.jar", "mods/changed.jar", "mods/removed_by_admin.jar"});

    // --- Manifeste distant (ServerInfo) — mods référencés depuis la banque ---
    ServerInfo srv;
    srv.id = "test"; srv.minecraftVersion = "1.20.1"; srv.loader = "fabric"; srv.valid = true;

    ModEntry ok;      ok.file = "keep_ok.jar";
    ok.sha256 = SyncManager::sha256File(mgr.absPath("mods/keep_ok.jar")); // hash conforme
    ModEntry changed; changed.file = "changed.jar";
    changed.sha256 = SyncManager::sha256File(mgr.absPath("mods/keep_ok.jar")); // hash != local
    ModEntry missing; missing.file = "missing.jar";
    missing.sha256 = "deadbeef";
    srv.mods = { ok, changed, missing };

    out << "Scénario de synchro sur " << dir << "\n";

    // --- Calcul du plan ---
    const SyncPlan plan = mgr.computePlan(srv);

    auto contains = [](const QVector<AssetRef> &v, const QString &file) {
        for (const AssetRef &e : v) if (e.entry.file == file) return true;
        return false;
    };

    out << "\nPlan calculé :\n";
    check(plan.upToDate.size() == 1 && contains(plan.upToDate, "keep_ok.jar"),
          "keep_ok.jar → à jour");
    check(plan.toDownload.size() == 2, "2 fichiers à télécharger");
    check(contains(plan.toDownload, "changed.jar"),
          "changed.jar (hash différent) → à télécharger");
    check(contains(plan.toDownload, "missing.jar"),
          "missing.jar (absent) → à télécharger");
    check(plan.toDelete.size() == 1 && plan.toDelete.contains("mods/removed_by_admin.jar"),
          "removed_by_admin.jar → à supprimer (retiré du manifeste)");
    check(!plan.toDelete.contains("mods/player_added.jar"),
          "player_added.jar JAMAIS dans les suppressions (inconnu du registre)");

    // --- Application des suppressions ---
    const int removed = mgr.applyDeletions(plan);
    out << "\nApplication des suppressions (" << removed << " supprimé) :\n";
    check(removed == 1, "1 fichier réellement supprimé");
    check(!QFileInfo::exists(mgr.absPath("mods/removed_by_admin.jar")),
          "removed_by_admin.jar effacé du disque");
    check(QFileInfo::exists(mgr.absPath("mods/player_added.jar")),
          "player_added.jar TOUJOURS présent (non-destructif) ✔");
    check(!mgr.loadInstalled().contains("mods/removed_by_admin.jar"),
          "registre nettoyé du fichier supprimé");
    check(mgr.loadInstalled().contains("mods/keep_ok.jar"),
          "registre garde les fichiers encore gérés");

    out << (failures == 0 ? "\nTOUS LES TESTS PASSENT ✅\n"
                          : QString("\n%1 test(s) en échec ❌\n").arg(failures));
    out.flush();
    return failures == 0 ? 0 : 1;
}
