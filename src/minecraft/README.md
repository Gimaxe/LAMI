# minecraft — Téléchargement & lancement du jeu

Cœur technique le plus lourd. Réimplémente le rôle de `minecraft-java-core`.
Référence : Prism Launcher, `launcher/minecraft/` (meta, download, launch).

## À coder
- **Version manifest** Mojang : `https://launchermeta.mojang.com/mc/game/version_manifest_v2.json`
  → puis le JSON de la version cible (libraries, assets, mainClass, arguments).
- **Assets** : `assetIndex` → objets dans `assets/objects/<2 premiers chars du hash>/<hash>`.
- **Libraries** : téléchargement + filtrage par OS (rules), build du **classpath**.
- **Mod loaders** : Fabric (`meta.fabricmc.net`) et Forge (endpoints Forge) — injecter
  leurs libraries et la bonne mainClass.
- **Java** : sélectionner/installer le bon JRE selon la version MC cible.
- **Lancement** : construire la ligne de commande JVM (auth token, UUID, pseudo,
  natives, gameDir isolé par serveur) et lancer via `QProcess`.

## Points de conception LAMI
- **Instances isolées par serveur** (un gameDir par `servers/<id>`).
- **Mode hors-ligne** : si une install locale complète existe déjà mais que la sync
  échoue (pas d'internet), permettre le lancement solo quand même.

Outils Qt : `QNetworkAccessManager`, `QJsonDocument`, `QProcess`, `QCryptographicHash`.
