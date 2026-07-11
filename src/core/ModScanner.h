#pragma once

#include <QString>
#include <QVector>

#include "github/Models.h"

namespace lami {

// Scanne un dossier de mods (les .jar) et construit la liste de références
// (nom de fichier + SHA256 + taille) prête à mettre dans un ServerInfo.
// L'Hébergeur n'a qu'à pointer son dossier `mods/` : le reste est calculé.
// Résultat trié par nom de fichier. Pur / hors-ligne → testable headless.
QVector<ModEntry> scanModsFolder(const QString &dir);

} // namespace lami
