# LAMI — Launcher Atraxe MInecraft

Launcher Minecraft alternatif (Windows / macOS / Linux) en **C++/Qt 6**.
Un joueur non-technique tape juste l'IP d'un serveur modé : LAMI télécharge et configure
tout seul la bonne version, le mod loader (Forge/Fabric), les mods et resource packs.

## Principe d'architecture
- **Pas de backend.** Un repo GitHub unique sert de base de données
  (`servers/<id>.json`, `mods/<id>/`, `roles.json`).
- **Identité = compte Microsoft** du joueur (obligatoire pour lancer MC), via l'UUID
  Minecraft → système de 3 rôles (Joueur / Hébergeur / Super Admin).
- **Sync non-destructive** par SHA256 : ne touche jamais aux ajouts manuels du joueur.

## Stack
Qt 6 (Widgets) · CMake · `QNetworkAccessManager` (HTTP) · `QJsonDocument` (JSON) ·
`QCryptographicHash` (SHA256) · `QProcess` (lancement JVM).
Référence pour l'auth Microsoft et le lancement du jeu : **Prism Launcher** (open-source).

## Arborescence
```
LAMI/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── ui/          # fenêtres Qt (coquille "Rejoindre un serveur" en place)
│   ├── auth/        # auth Microsoft (device code -> Xbox -> XSTS -> Minecraft) + vérif UUID
│   ├── minecraft/   # version manifest, download assets/libs, classpath, launch JVM, Forge/Fabric
│   ├── sync/        # comparaison SHA256, sync non-destructive
│   ├── github/      # client API GitHub (repo-BDD : lecture/écriture serveurs & mods)
│   └── roles/       # 3 rôles via UUID Minecraft
└── resources/       # icônes, styles
```
Chaque sous-dossier de `src/` contient un `README.md` détaillant ce qu'il doit faire.

## Prérequis de build (Ubuntu / Debian)
```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev
```

## Compiler & lancer
```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/lami
```

État : **squelette qui compile** — la fenêtre principale s'ouvre ; la logique métier
(auth, sync, lancement) reste à implémenter module par module.
