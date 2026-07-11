#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

// Configuration runtime du launcher (valeurs simples pour l'instant ;
// à terme certaines viendront d'un fichier de config utilisateur).
namespace lami {
namespace config {

inline QString readFileTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

// Cherche un fichier secret d'abord À CÔTÉ de l'exécutable, sinon dans ~/LAMI.
inline QString readSecret(const QString &name)
{
    const QString beside = QCoreApplication::applicationDirPath() + "/" + name;
    const QString value = readFileTrimmed(beside);
    if (!value.isEmpty())
        return value;
    return readFileTrimmed(QDir::homePath() + "/LAMI/" + name);
}

// Secrets locaux (jamais committés) : voir DESKTOP.md.
inline QString token()    { return readSecret(".token"); }
inline QString clientId() { return readSecret(".client_id"); }

// Coordonnées du repo-BDD.
inline QString owner()  { return "Gimaxe"; }
inline QString repo()   { return "LAMI-db"; }
inline QString branch() { return "main"; }

// Emplacements locaux.
inline QString dataRoot() { return QDir::homePath() + "/.local/share/LAMI"; }
inline QString javaPath() { return "java"; }  // JRE auto selon version = TODO

} // namespace config
} // namespace lami
