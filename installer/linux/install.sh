#!/bin/sh
# Installeur Linux de LAMI (par utilisateur, sans root, compatible auto-update).
# Copie l'app dans ~/.local/share/LAMI et crée une entrée de menu + icône.
set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
APP="$HOME/.local/share/LAMI"
DESKTOP_DIR="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"

echo "Installation de LAMI dans $APP …"
mkdir -p "$APP" "$DESKTOP_DIR" "$ICON_DIR"

# Copie des fichiers de l'app (tout sauf les scripts d'install).
for item in lami_backend lami_shell web; do
    [ -e "$SRC/$item" ] && cp -a "$SRC/$item" "$APP/"
done
chmod +x "$APP/lami_shell" "$APP/lami_backend" 2>/dev/null || true

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

# Désinstalleur.
cat > "$APP/uninstall.sh" <<EOF
#!/bin/sh
echo "Désinstallation de LAMI…"
rm -rf "$APP"
rm -f "$DESKTOP_DIR/lami.desktop" "$ICON_DIR/lami.png"
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
echo "LAMI désinstallé."
EOF
chmod +x "$APP/uninstall.sh"

echo "LAMI installé. Lance-le depuis le menu d'applications, ou : $APP/lami_shell"
echo "Pour désinstaller : $APP/uninstall.sh"
