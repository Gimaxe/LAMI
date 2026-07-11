# auth — Authentification Microsoft & identité

Réimplémentation de ce que `minecraft-java-core` faisait côté npm.
Référence : Prism Launcher, `launcher/minecraft/auth/`.

## Flux à coder
1. **Device code flow** Microsoft (l'utilisateur entre un code sur une page MS).
2. Échange du token MS → **Xbox Live** (`user.auth.xboxlive.com`).
3. Xbox Live → **XSTS** (`xsts.auth.xboxlive.com`).
4. XSTS → **token Minecraft** (`api.minecraftservices.com/authentication/login_with_xbox`).
5. Récupération du **profil** (UUID + pseudo) via `api.minecraftservices.com/minecraft/profile`.

## Rôle dans LAMI
- L'**UUID Minecraft** obtenu ici est l'identité pour le système de rôles (voir `src/roles`).
- **Vérif renforcée** : avant toute action sensible (publier un serveur, modifier les
  rôles), re-vérifier l'UUID auprès de Microsoft/Mojang avec le vrai jeton de session,
  pour empêcher qu'un fichier de config local trafiqué usurpe un rôle.
- Stockage du refresh token de façon persistante (relance sans re-login).

Outils Qt : `QNetworkAccessManager` (HTTP), `QJsonDocument` (payloads).
