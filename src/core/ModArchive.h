#pragma once

#include <QString>
#include <QStringList>

namespace lami {

// Extraction d'archives de mods (ZIP) via miniz (vendorisé, autonome).
// L'Hébergeur peut fournir un .zip de mods au lieu d'un dossier.
namespace ModArchive {

// Extrait les fichiers d'un ZIP vers `destDir` (noms aplatis : on ne garde que le
// nom de fichier). `ext` filtre par extension (ex. ".jar") ou "" = tous les fichiers.
// Retourne la liste des noms extraits ; vide en cas d'échec.
QStringList extract(const QString &zipPath, const QString &destDir,
                    const QString &ext, QString *error = nullptr);

// Raccourci : uniquement les *.jar.
QStringList extractJars(const QString &zipPath, const QString &destDir,
                        QString *error = nullptr);

} // namespace ModArchive

} // namespace lami
