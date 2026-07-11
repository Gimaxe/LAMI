#pragma once

#include <QString>
#include <QStringList>

namespace lami {

// Extraction d'archives de mods (ZIP) via miniz (vendorisé, autonome).
// L'Hébergeur peut fournir un .zip de mods au lieu d'un dossier.
namespace ModArchive {

// Extrait tous les fichiers *.jar d'un ZIP vers `destDir` (noms aplatis :
// on ne garde que le nom de fichier, l'arborescence interne est ignorée).
// Retourne la liste des noms de fichiers extraits ; vide en cas d'échec.
QStringList extractJars(const QString &zipPath, const QString &destDir,
                        QString *error = nullptr);

} // namespace ModArchive

} // namespace lami
