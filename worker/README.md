# LAMI Worker — intermédiaire de confiance

Ce Worker Cloudflare est la **seule** entité qui écrit dans `LAMI-db`. Il garantit :

- **Aucun token GitHub côté client** — il vit uniquement ici (secret `GITHUB_TOKEN`).
- **Identité inviolable** — le Worker redemande l'UUID à **Mojang** à partir du
  token Minecraft du joueur. Le client ne peut pas mentir sur son identité.
- **Rôles/propriété imposés côté serveur** — un hébergeur ne peut modifier /
  supprimer que **ses** serveurs ; seul le super admin gère les rôles.

> ⚠️ **Prérequis** : il faut que l'authentification Microsoft/Minecraft
> fonctionne (app Azure approuvée) pour que le client obtienne un token
> Minecraft valide. Tant que ce n'est pas le cas, le Worker est prêt mais
> inactif (c'est le plan : brancher plus tard).

## Endpoints (POST, corps JSON)

Tous attendent `{ "token": "<access token Minecraft>" , ... }`.

| Route | Rôle requis | Corps | Effet |
|-------|-------------|-------|-------|
| `/whoami` | — | `{token}` | Renvoie `{uuid,name,role}` (identité vérifiée par Mojang) |
| `/publish` | host/superadmin | `{token, server, assets}` | Publie ; `owner` = uuid authentifié |
| `/edit` | propriétaire ou superadmin | `{token, id, changes, assets}` | Modifie ses serveurs |
| `/delete` | propriétaire ou superadmin | `{token, id}` | Supprime ses serveurs + index |
| `/setRole` | superadmin | `{token, uuid, role}` | Écrit roles.json |
| `/removeRole` | superadmin | `{token, uuid}` | Retire un rôle |

`assets` (optionnel) : `{ "mods": [{file, base64, sha256, size}], "plugins": [...], "resourcepacks": [...], "shaders": [...] }`.

## Déploiement

```bash
cd worker
npm install
npx wrangler login                 # connexion Cloudflare
npx wrangler secret put GITHUB_TOKEN   # coller le PAT fine-grained (Contents R/W sur LAMI-db)
npx wrangler deploy                # -> https://lami-worker.<compte>.workers.dev
```

Ajuste `GH_OWNER/GH_REPO/GH_BRANCH` dans `wrangler.toml` si besoin.

## Test rapide (une fois l'auth OK)

```bash
curl -X POST https://lami-worker.<compte>.workers.dev/whoami \
  -H 'Content-Type: application/json' \
  -d '{"token":"<ACCESS_TOKEN_MINECRAFT>"}'
# -> {"uuid":"...","name":"...","role":"superadmin"}
```

## Intégration côté launcher (C++), à faire au moment du branchement

1. Réglage `workerUrl` (déjà prévu côté config) : si défini, les **écritures**
   (`publishServer`, `editServer`, `deleteServer`, `setRole`, `removeRole`)
   passent par le Worker au lieu de l'API GitHub directe.
2. Le launcher envoie `m_session.minecraftToken` (le vrai token, une fois l'auth
   Microsoft approuvée) dans le champ `token`.
3. Les **lectures** (résolution de serveur, téléchargement de mods) peuvent
   rester directes (repo public) ou passer aussi par le Worker si le repo reste
   privé (ajouter un endpoint `/read` au besoin).
4. Stocker le token Minecraft de façon chiffrée : DPAPI (Windows), libsecret
   (Linux), Keychain (macOS).

Tant que `workerUrl` est vide, le launcher garde le comportement actuel.
