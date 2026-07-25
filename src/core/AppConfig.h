#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
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

// client_id Azure (non secret). Le jeton GitHub (token()) est défini plus bas
// car il consulte aussi le dossier de données.
// client_id de l'app Azure. NON secret (identifiant public de l'OAuth) : on le
// met en dur par défaut pour que la connexion marche sur l'app installée, tout
// en laissant un fichier .client_id le surcharger si besoin.
inline QString clientId()
{
    const QString c = readSecret(".client_id");
    return c.isEmpty() ? QStringLiteral("7b5b947c-816f-45a5-992f-c43968fd4b15") : c;
}

// Coordonnées du repo-BDD.
inline QString owner()  { return "Gimaxe"; }
inline QString repo()   { return "LAMI-db"; }
inline QString branch() { return "main"; }

// Emplacement PAR DÉFAUT des données (et du fichier de réglages, qui y reste
// toujours pour ne pas se perdre quand l'utilisateur change le dossier du jeu).
// Emplacement STANDARD par OS : Windows -> %APPDATA%\Roaming\LAMI,
// Linux -> ~/.local/share/LAMI, macOS -> ~/Library/Application Support/LAMI.
inline QString defaultDataRoot()
{
    const QString p = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return p.isEmpty() ? (QDir::homePath() + "/.local/share/LAMI") : p;
}
inline QString settingsFile() { return QDir(defaultDataRoot()).filePath("settings.json"); }

inline QJsonObject readSettings()
{
    QFile f(settingsFile());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// Chemin du jeton dans le dossier de données (persiste entre réinstallations,
// car séparé du dossier d'installation de l'app).
inline QString tokenFile() { return QDir(defaultDataRoot()).filePath(".token"); }
// Refresh token Microsoft sauvegardé (« se souvenir de moi »). Stocké en local.
inline QString refreshTokenFile() { return QDir(defaultDataRoot()).filePath(".msrefresh"); }

// Jeton GitHub (fine-grained PAT). Ordre : à côté de l'exe, puis ~/LAMI, puis le
// dossier de données (défini via l'UI). Jamais committé ni journalisé.
inline QString token()
{
    const QString t = readSecret(".token");
    if (!t.isEmpty()) return t;
    return readFileTrimmed(tokenFile());
}

// Emplacements locaux (surchargeables via les réglages).
inline QString dataRoot()
{
    const QString d = readSettings().value("dataRoot").toString().trimmed();
    return d.isEmpty() ? defaultDataRoot() : d;
}
inline QString javaPath()
{
    // Java personnalisé si renseigné, sinon "java" (repli ; un JRE 17 est de
    // toute façon provisionné automatiquement pour chaque version).
    const QString j = readSettings().value("javaPath").toString().trimmed();
    return j.isEmpty() ? QStringLiteral("java") : j;
}
// Arguments JVM additionnels saisis par l'utilisateur (chaîne brute).
inline QString jvmArgs() { return readSettings().value("jvmArgs").toString(); }
// URL du Worker de confiance. Figée en dur (comme client_id) : c'est LUI qui
// détient le token GitHub et impose rôles/propriété. Un réglage peut la
// surcharger (dev), mais par défaut tout le monde passe par le vrai Worker.
// NB sécurité : la changer côté client ne permet PAS d'altérer le repo — seul
// ce Worker-ci possède le token d'écriture ; un faux Worker ne trompe que
// l'affichage local de l'attaquant, jamais la base.
inline QString workerUrl()
{
    const QString w = readSettings().value("workerUrl").toString().trimmed();
    return w.isEmpty() ? QStringLiteral("https://lami-worker.lami-atraxe.workers.dev") : w;
}
// Comportement de fermeture du launcher au lancement du jeu.
inline QString closeBehavior() { return readSettings().value("closeBehavior").toString(); }
// L'utilisateur veut-il forcer SON java (au lieu du JRE auto) ?
inline bool forceCustomJava() { return !readSettings().value("javaPath").toString().trimmed().isEmpty(); }

} // namespace config
} // namespace lami
