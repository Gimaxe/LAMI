#!/bin/sh
# Installeur Linux de LAMI (par utilisateur, sans root, compatible auto-update).
# L'application va dans ~/.local/lib/LAMI ; les DONNÉES du joueur (instances,
# skins, réglages) restent dans ~/.local/share/LAMI et ne sont jamais touchées.
set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
APP="$HOME/.local/lib/LAMI"
DATA="$HOME/.local/share/LAMI"
DESKTOP_DIR="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"

echo "Installation de LAMI dans $APP …"
mkdir -p "$APP" "$DESKTOP_DIR" "$ICON_DIR"

# Copie des fichiers de l'app (tout sauf les scripts d'install).
# lib/, plugins/ et qt.conf portent le Qt embarqué : sans eux le moteur ne
# démarrerait que sur une machine où Qt 6 est déjà installé.
for item in lami_backend lami_shell web lib plugins qt.conf; do
    [ -e "$SRC/$item" ] && cp -a "$SRC/$item" "$APP/"
done
chmod +x "$APP/lami_shell" "$APP/lami_backend" 2>/dev/null || true

# --- Dépendances : lami_backend a besoin de Qt 6 (Core, Network, WebSockets).
# lami_shell, lui, n'en dépend pas : sans Qt la fenêtre s'ouvre normalement mais
# le launcher affiche « Backend non connecté ». On prévient donc explicitement.
MISSING="$(ldd "$APP/lami_backend" 2>/dev/null | grep 'not found' | awk '{print $1}' | sort -u || true)"
if [ -n "$MISSING" ]; then
    echo
    echo "/!\\ Bibliothèques manquantes pour le moteur de LAMI :"
    echo "$MISSING" | sed 's/^/     /'
    echo
    if command -v apt-get >/dev/null 2>&1; then
        echo "   Installe-les avec :"
        echo "     sudo apt install -y libqt6core6 libqt6network6 libqt6websockets6"
        echo "   (selon la distribution : sudo apt install -y qt6-base-dev qt6-websockets-dev)"
    elif command -v dnf >/dev/null 2>&1; then
        echo "   Installe-les avec :"
        echo "     sudo dnf install -y qt6-qtbase qt6-qtwebsockets"
    elif command -v pacman >/dev/null 2>&1; then
        echo "   Installe-les avec :"
        echo "     sudo pacman -S --needed qt6-base qt6-websockets"
    fi
    echo
    echo "   LAMI est installé, mais ces paquets sont nécessaires pour qu'il fonctionne."
    echo
fi

# Icône.
[ -f "$APP/web/assets/lami-icon.png" ] && cp -f "$APP/web/assets/lami-icon.png" "$ICON_DIR/lami.png"

# Entrée de menu.
cat > "$DESKTOP_DIR/lami.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LAMI
Comment=Launcher Minecraft LAMI
Exec=$APP/lami_shell
Icon=lami
Terminal=false
Categories=Game;
EOF
chmod +x "$DESKTOP_DIR/lami.desktop" 2>/dev/null || true
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

# --- Ancienne installation (l'app était copiée DANS le dossier de données) :
# on retire uniquement les fichiers de l'app, jamais les instances ni les skins.
if [ -f "$DATA/lami_shell" ]; then
    echo "Nettoyage de l'ancienne installation (les données sont conservées)…"
    rm -f "$DATA/lami_shell" "$DATA/lami_backend" "$DATA/uninstall.sh"
    rm -rf "$DATA/web"
fi

# Désinstalleur : ne supprime QUE l'application.
cat > "$APP/uninstall.sh" <<EOF
#!/bin/sh
echo "Désinstallation de LAMI…"
rm -rf "$APP"
rm -f "$DESKTOP_DIR/lami.desktop" "$ICON_DIR/lami.png"
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
echo "LAMI désinstallé."
echo "Tes données (instances, skins, réglages) sont conservées dans :"
echo "  $DATA"
echo "Pour les supprimer aussi :  rm -rf \"$DATA\""
EOF
chmod +x "$APP/uninstall.sh"

echo "LAMI installé. Lance-le depuis le menu d'applications, ou : $APP/lami_shell"
echo "Pour désinstaller : $APP/uninstall.sh"
