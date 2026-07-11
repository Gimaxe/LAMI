# sync — Synchronisation non-destructive des fichiers

Compare l'installation locale d'un serveur au manifeste distant et met à jour
uniquement ce qui a changé. **Ne touche jamais** à ce que le joueur a ajouté lui-même.

## Algorithme
1. Charger le manifeste du serveur (liste `{ chemin, sha256, taille }`).
2. Pour chaque fichier du manifeste : calculer le **SHA256** local (`QCryptographicHash`).
   - Absent ou hash différent → (re)télécharger.
   - Hash identique → rien à faire.
3. **Suppression prudente** : un fichier local peut être supprimé UNIQUEMENT s'il a été
   téléchargé par le launcher (tracé dans un registre local, p.ex. `.lami/installed.json`)
   ET qu'il a disparu du manifeste. Tout fichier inconnu du launcher
   (mods/shaders/resource packs ajoutés à la main par le joueur) est **intouchable**.

## Règles
- Sync déclenchée **au démarrage** ou sur clic manuel — jamais en arrière-plan.
- Écrire le registre `installed.json` avant/après pour savoir ce qui appartient au launcher.

Outils Qt : `QCryptographicHash` (SHA256), `QFile`/`QDir`, `QJsonDocument`.
