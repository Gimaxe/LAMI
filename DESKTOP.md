# LAMI — Build & lancement sur PC de bureau

Le VPS n'a pas d'écran : pour **voir** l'app et **lancer Minecraft**, on compile sur
ta machine de bureau (Windows / macOS / Linux). Toute la logique est déjà codée et
testée ; ce guide sert à la faire tourner en vrai.

---

## 1. Prérequis

| Outil | Windows | macOS | Linux (Debian/Ubuntu) |
|-------|---------|-------|------------------------|
| Compilateur C++ | Visual Studio 2022 (Desktop C++) | Xcode CLT (`xcode-select --install`) | `sudo apt install build-essential` |
| CMake | [cmake.org](https://cmake.org/download/) | `brew install cmake` | `sudo apt install cmake` |
| Qt 6 (Widgets) | [Qt Online Installer](https://www.qt.io/download-qt-installer) | `brew install qt` | `sudo apt install qt6-base-dev` |
| Java (pour lancer MC) | [Adoptium JRE 17](https://adoptium.net/) | `brew install temurin` | `sudo apt install openjdk-17-jre` |

> Java 17 convient pour Minecraft 1.20.1. D'autres versions MC peuvent exiger un
> autre Java (le provisioning automatique du JRE est une amélioration prévue).

## 2. Récupérer le projet

Copie le dossier `~/LAMI` du VPS vers ta machine (scp, git, clé USB…). **Copie aussi**
les deux fichiers de config qui ne sont pas dans le code :

```
~/LAMI/.token       # ton token GitHub (repo privé)
~/LAMI/.client_id   # ton client_id Azure
```

Place-les au même endroit relatif (dans le dossier `LAMI/` chez toi). Ils sont lus au
runtime et ne doivent jamais être committés.

## 3. Compiler

```bash
cd LAMI
cmake -S . -B build
cmake --build build --config Release -j
```

L'exécutable apparaît dans `build/` (`lami` ou `build/Release/lami.exe` sous Windows).

## 4. Tester sans écran (sanity check)

```bash
./build/lami --plan exemple
```

Doit afficher le plan (~700 Mo, 3661 fichiers) et la **commande de lancement** Fabric
complète. Si tu vois ça, toute la chaîne fonctionne chez toi.

## 5. Lancer l'interface

```bash
./build/lami
```

La fenêtre « Rejoindre un serveur » s'ouvre. Tape `exemple`, clique **Rejoindre** :
les infos du serveur s'affichent (la connexion/téléchargement réel se branche ensuite).

## 6. Lancer Minecraft pour de vrai

⚠️ **Nécessite que l'app Azure soit approuvée par Microsoft** (voir aka.ms/AppRegInfo).
Tant que l'auth n'est pas validée, `--accessToken` reste factice et Minecraft refusera
la session. Une fois l'approbation obtenue :

1. l'auth Microsoft fournira un vrai token + UUID,
2. l'orchestrateur téléchargera les ~700 Mo dans `~/.local/share/LAMI/`,
3. puis lancera la commande de la section 4 via `QProcess`.

Les natives LWJGL (1.19+) sont auto-extraites par LWJGL dans le dossier `natives`
défini par la commande — rien à faire de spécial.

---

## Lancer les tests (facultatif)

```bash
cmake --build build --target lami_test_instance
./build/lami_test_instance
```
Cibles disponibles : `lami_test_github`, `_sync`, `_roles`, `_minecraft`, `_download`,
`_launch`, `_fabric`, `_instance`, `_publish`, `_auth`.
