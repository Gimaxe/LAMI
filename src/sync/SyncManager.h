#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "github/Models.h"

namespace lami {

// Un asset à télécharger, avec sa catégorie (mods/plugins/resourcepacks/shaders)
// pour retrouver son chemin en banque et son dossier local.
struct AssetRef {
    QString   type;    // assets::Mods / Plugins / ResourcePacks / Shaders
    ModEntry  entry;
};

// Résultat du calcul de synchro (voir src/sync/README.md).
struct SyncPlan {
    QVector<AssetRef> toDownload;  // absents localement ou hash différent
    QVector<QString>  toDelete;    // installés par le launcher, retirés du manifeste
    QVector<AssetRef> upToDate;    // présents et hash conforme

    bool nothingToDo() const { return toDownload.isEmpty() && toDelete.isEmpty(); }
};

// Synchronisation NON-DESTRUCTIVE d'une instance de serveur.
//
// Principe : on ne supprime un fichier local QUE s'il a été installé par le
// launcher (tracé dans <instance>/.lami/installed.json) ET qu'il a disparu du
// manifeste. Tout fichier inconnu du registre (mods/shaders/packs ajoutés à la
// main par le joueur) est intouchable.
//
// SyncManager calcule le plan et applique les suppressions sûres. Les
// téléchargements (réseau) sont exécutés ailleurs, puis confirmés ici via
// markInstalled().
class SyncManager
{
public:
    explicit SyncManager(QString instanceDir);

    const QString &instanceDir() const { return m_instanceDir; }

    // SHA256 hex d'un fichier, lu par blocs. Chaîne vide si illisible.
    static QString sha256File(const QString &absolutePath);

    // Registre des fichiers gérés par le launcher (.lami/installed.json).
    QStringList loadInstalled() const;
    bool        saveInstalled(const QStringList &relativePaths) const;

    // Calcule le plan de synchro (aucune écriture disque).
    SyncPlan computePlan(const ServerInfo &server) const;

    // Applique UNIQUEMENT les suppressions sûres du plan et retire ces chemins
    // du registre. Retourne le nombre de fichiers supprimés.
    int applyDeletions(const SyncPlan &plan) const;

    // À appeler après un téléchargement réussi : ajoute les chemins au registre.
    bool markInstalled(const QStringList &relativePaths) const;

    // Chemin absolu d'un fichier relatif de l'instance.
    QString absPath(const QString &relativePath) const;

private:
    QString registryPath() const;  // <instance>/.lami/installed.json

    QString m_instanceDir;
};

} // namespace lami
