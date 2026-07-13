#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QJsonObject>

// Modèles de données du repo-BDD GitHub.
// Voir src/github/README.md pour le schéma des fichiers.

namespace lami {

// Un mod référencé par un serveur. Le fichier réel vit dans la BANQUE mutualisée
// du repo (mods/<version>/<loader>/<file>), partagée entre serveurs pour éviter
// les doublons. Le serveur ne stocke que la référence (nom + hash + taille).
struct ModEntry {
    QString file;    // nom du fichier, ex. "sodium-fabric-0.5.3.jar"
    QString sha256;  // hash attendu (dédoublonnage + sync non-destructive)
    qint64  size = 0;
};

// Les 4 catégories d'assets d'un serveur (chaînes utilisées côté JSON/JS).
namespace assets {
constexpr const char *Mods          = "mods";
constexpr const char *Plugins       = "plugins";
constexpr const char *ResourcePacks = "resourcepacks";
constexpr const char *Shaders       = "shaders";
}

// Contenu de servers/<id>.json
struct ServerInfo {
    QString id;                // identifiant du serveur (nom du fichier json)
    QString name;              // nom affiché
    QString address;           // IP / sous-domaine à rejoindre
    QString minecraftVersion;  // ex. "1.20.1"
    QString loader;            // "vanilla" | "fabric" | "quilt" | "forge" | "neoforge"
    QString loaderVersion;     // ex. "0.15.11" (vide si vanilla)
    QVector<ModEntry> mods;          // mods (client)
    QVector<ModEntry> plugins;       // plugins (serveur)
    QVector<ModEntry> resourcePacks; // resource packs (client)
    QVector<ModEntry> shaders;       // shaders (client)
    QString owner;             // UUID Minecraft du créateur (pour les droits de modif/suppression)
    bool    valid = false;     // false si le parsing a échoué

    // Accès à une catégorie par son nom (assets::*).
    const QVector<ModEntry> &assetList(const QString &type) const;
    QVector<ModEntry> &assetList(const QString &type);
};

// Chemin LOCAL d'un asset dans l'instance (relatif au gameDir), ex. "mods/sodium.jar".
QString assetLocalPath(const QString &type, const ModEntry &e);

// Chemin de l'asset dans la BANQUE du repo (mutualisé) :
//   mods/<version>/<loader>/<file> · plugins/<version>/<file>
//   resourcepacks/<version>/<file> · shaders/<version>/<file>
QString assetBankPath(const ServerInfo &server, const QString &type, const ModEntry &e);

// Raccourcis mods (compat).
inline QString modLocalPath(const ModEntry &mod) { return assetLocalPath(assets::Mods, mod); }
inline QString modBankPath(const ServerInfo &s, const ModEntry &m) { return assetBankPath(s, assets::Mods, m); }

// Les 3 rôles (voir src/roles/README.md), déduits de l'UUID Minecraft.
enum class Role {
    Player,      // défaut
    Host,        // Hébergeur
    SuperAdmin,  // Super Admin
};

// Contenu de roles.json : mapping UUID Minecraft -> rôle.
using RoleTable = QHash<QString, Role>;

// Conversions texte <-> enum, pour parser/écrire roles.json.
Role   roleFromString(const QString &s);   // inconnu -> Player
QString roleToString(Role role);

// Sérialise un ServerInfo au format canonique de servers/<id>.json (l'inverse
// du parsing de GitHubClient). Utilisé pour la publication.
QJsonObject serverToJson(const ServerInfo &server);

} // namespace lami
