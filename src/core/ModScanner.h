#pragma once

#include <QString>
#include <QVector>

#include "github/Models.h"

namespace lami {

// Scanne un dossier de mods (les .jar) et construit la liste de références
// (nom de fichier + SHA256 + taille) prête à mettre dans un ServerInfo.
// L'Hébergeur n'a qu'à pointer son dossier `mods/` : le reste est calculé.
// Résultat trié par nom de fichier. Pur / hors-ligne → testable headless.
// Scanne les fichiers d'un dossier (filtres optionnels, ex. {"*.jar"} ; vide = tous)
// → nom + sha256 + taille, trié par nom.
QVector<ModEntry> scanFolder(const QString &dir, const QStringList &filters = {});

// Raccourci : uniquement les .jar (mods).
QVector<ModEntry> scanModsFolder(const QString &dir);

} // namespace lami
