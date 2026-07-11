# github — Client de la "base de données" GitHub

LAMI n'a **pas de backend**. Un repo GitHub unique sert de base de données.
Ce module lit/écrit ce repo via l'API GitHub (`api.github.com`) et les raw files.

## Contenu du repo-BDD
```
servers/<id>.json     # infos serveur : version, loader, liste mods {chemin, sha256}
mods/<id>/...         # fichiers réels des mods d'un serveur
roles.json            # racine : mapping UUID Minecraft -> rôle
```

## Opérations
- **Lecture (tous)** : récupérer `servers/<id>.json`, `roles.json`, télécharger les mods
  (via URLs raw / API contents). Pas besoin de token pour du public.
- **Écriture (Hébergeur / Super Admin)** : créer/mettre à jour `servers/<id>.json` et les
  mods via l'API contents (`PUT /repos/{owner}/{repo}/contents/{path}`, commit direct).

## Tokens
- Tokens GitHub **mutualisés par rôle** (un token "Hébergeur" partagé, un token "Admin"),
  jamais un par personne. L'utilisateur ne voit **jamais** de token — tout passe par l'UI.
- Le token correspondant au rôle est fourni à l'app selon l'UUID (voir `src/roles`).

## Supervision Discord (prévue)
- Publication/màj serveur → **GitHub Action** déclenchée par le commit → webhook Discord
  (secret côté repo, pas côté client).
- 1er téléchargement d'un serveur par un joueur → à trancher : webhook direct depuis le
  client, ou petit relais serverless.

Outils Qt : `QNetworkAccessManager`, `QJsonDocument`.
