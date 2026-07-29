# LAMI — Launcher Atraxe MInecraft

Launcher Minecraft alternatif (**Windows / Linux**) en **C++/Qt 6**.
Un joueur tape l'adresse d'un serveur : LAMI installe tout seul la bonne version de
Minecraft, le mod loader, les mods, resource packs et shaders — puis lance le jeu.

---

## Télécharger

| Plateforme | Téléchargement | Remarque |
|---|---|---|
| **Windows** | [**Installeur (.exe)**](https://github.com/Gimaxe/LAMI/releases/latest/download/LAMI-Setup.exe) | recommandé — installe et crée les raccourcis |
| **Windows** | [Version portable (.zip)](https://github.com/Gimaxe/LAMI/releases/latest/download/LAMI-windows.zip) | à décompresser, aucune installation |
| **Linux** | [Archive (.tar.gz)](https://github.com/Gimaxe/LAMI/releases/latest/download/LAMI-linux.tar.gz) | contient `install.sh` |

Ces liens pointent **toujours vers la dernière version**.
[Toutes les versions et notes de publication →](https://github.com/Gimaxe/LAMI/releases/latest)

Installation Linux :
```bash
tar xzf LAMI-linux.tar.gz && ./install.sh
```

Le launcher se met à jour tout seul : il vérifie au démarrage et propose l'installation.

---

## Ce que fait LAMI

- **Rejoindre** : entrer l'adresse d'un serveur → installation complète automatique
  (version Minecraft, Forge/Fabric/NeoForge/Quilt, mods, packs, shaders).
- **Synchronisation** à chaque lancement : compare le manifeste du serveur au disque,
  télécharge ce qui manque, retire ce que l'hébergeur a supprimé, **sans jamais toucher
  aux fichiers ajoutés par le joueur** (comparaison par empreinte SHA-256).
- **Publier / modifier** un serveur (rôle Hébergeur) : dépôt d'archives `.zip`/`.7z`,
  envoi par lots pour les gros modpacks.
- **Skins** : visualiseur 3D, bibliothèque de 3 emplacements, application du skin sur le
  compte Minecraft, sauvegarde du skin d'origine.
- **Administration** (Super Admin) : attribution et révocation des rôles.
- Jeu configuré en **français** et serveur pré-enregistré dans la liste multijoueur au
  premier lancement.

## Rôles

| Rôle | Droits |
|---|---|
| **Joueur** | rejoindre et installer des serveurs |
| **Hébergeur** | publier, modifier et supprimer **ses** serveurs |
| **Super Admin** | tout, plus la gestion des rôles |

Le rôle est lié à l'**UUID Minecraft** et relu à chaque action : une révocation prend
effet immédiatement, sans redémarrage du launcher.

---

## Architecture

```
Launcher (C++/Qt)  ──►  Worker Cloudflare  ──►  repo GitHub « LAMI-db »
  webview HTML           vérifie identité         base de données
  backend WebSocket      et rôles, détient        (manifestes, banque
                         le token GitHub           de mods, rôles)
                              │
                              └──►  Relais Deno  ──►  API Mojang
                                    (vérification du token Minecraft)
```

- **Aucun token GitHub côté client.** Le client envoie son token Minecraft ; le Worker
  redemande l'identité réelle à Mojang, calcule le rôle, vérifie la propriété du serveur,
  puis écrit lui-même dans le repo avec son propre token.
- **Relais d'identité** : les adresses IP des Workers Cloudflare sont bloquées par tous
  les domaines Mojang. Un micro-service sur Deno Deploy fait donc cette vérification à
  leur place ([`relay/`](relay/)).
- **Cache au bord** : les fichiers de mods (immuables) sont mis en cache par Cloudflare ;
  les métadonnées ne le sont jamais.
- **Banque mutualisée** : un mod partagé par plusieurs serveurs n'est stocké qu'une fois
  (`mods/<version>/<loader>/<fichier>`).

## Composants

```
LAMI/
├── src/
│   ├── bridge/      # pont WebSocket entre l'UI (JS) et le C++
│   ├── auth/        # Microsoft device code → Xbox → XSTS → Minecraft ; secrets chiffrés (DPAPI/libsecret)
│   ├── core/        # instances, plan de lancement, archives, publication
│   ├── minecraft/   # manifestes Mojang, téléchargements, Forge/Fabric, JVM
│   ├── sync/        # synchronisation non-destructive (SHA-256)
│   ├── github/      # lecture du repo-BDD (via le Worker)
│   └── shell/       # fenêtre native (WebView2 / WebKitGTK)
├── web/             # interface (HTML/JS, Three.js pour les skins)
├── worker/          # Worker Cloudflare (écritures, rôles, cache)
├── relay/           # relais de vérification d'identité (Deno Deploy)
└── installer/       # Inno Setup (Windows) et script d'installation (Linux)
```

Plusieurs sous-dossiers de `src/` contiennent un `README.md` détaillant leur rôle
(`auth`, `github`, `minecraft`, `roles`, `sync`).

---

## Compiler depuis les sources

Dépendances (Ubuntu / Debian) :
```bash
sudo apt install -y build-essential cmake pkg-config \
  qt6-base-dev qt6-websockets-dev libwebkit2gtk-4.1-dev libsecret-1-dev
```

Compilation et lancement :
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target lami_backend lami_shell
./build/lami_shell
```

Les binaires Windows et Linux sont produits ensemble par l'intégration continue à chaque
push sur `main` (voir [`.github/workflows/build.yml`](.github/workflows/build.yml)), qui
publie aussi la release.

## Déployer les services

```bash
# Worker Cloudflare (écritures + cache)
cd worker && npx wrangler deploy

# Relais d'identité : projet Deno Deploy créé à partir de relay/main.ts,
# avec la variable d'environnement RELAY_SECRET (identique au secret du Worker).
```
