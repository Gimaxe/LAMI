# roles — Système de rôles basé sur l'UUID Minecraft

Détermine ce qu'un utilisateur a le droit de faire, à partir de son **UUID Minecraft**
(fourni par `src/auth`) confronté à `roles.json` (racine du repo-BDD, lu via `src/github`).

## Les 3 rôles
| Rôle                | Droits                                                                 |
|---------------------|------------------------------------------------------------------------|
| **Joueur** (défaut) | Chercher un serveur par IP, le rejoindre, jouer                        |
| **Hébergeur**       | + Publier / mettre à jour ses propres serveurs depuis l'app           |
| **Super Admin**     | + Attribuer/retirer le rôle Hébergeur, superviser l'activité          |

## Points clés
- Le rôle sélectionne le **token GitHub** mutualisé à utiliser pour les écritures.
- **Anti-usurpation** : ne jamais faire confiance à un rôle déduit d'un fichier local.
  Avant une action sensible, re-vérifier l'UUID via `src/auth` (jeton de session réel)
  puis relire `roles.json` à la source.

Outils Qt : `QJsonDocument`.
