#!/bin/bash
# Construit le paquet Debian/Ubuntu de LAMI (.deb).
#
#   usage : installer/linux/build-deb.sh <version> [dossier-build] [sortie]
#
# Le paquet installe une VRAIE application système :
#   - visible dans le centre d'applications (GNOME Software, Discover), donc
#     désinstallable d'un clic, sans passer par un script ;
#   - dépendances déclarées (WebKitGTK, GTK) : « apt install ./LAMI.deb » tire
#     automatiquement tout ce qui manque ;
#   - Qt est embarqué dans le paquet (voir bundle.sh), rien d'autre à installer ;
#   - icône enregistrée dans le thème sous le nom « lami », ce qui permet au
#     bureau de l'afficher dans la barre des tâches (via StartupWMClass).
set -euo pipefail

VERSION="${1:?version manquante (ex. 0.73.0)}"
BUILD="${2:-build}"
OUT="${3:-.}"

ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT

APPDIR="$ROOT/opt/lami"
mkdir -p "$APPDIR" "$ROOT/DEBIAN" "$ROOT/usr/bin" \
         "$ROOT/usr/share/applications" \
         "$ROOT/usr/share/metainfo"

# --- Contenu de l'application (binaires + Qt embarqué + interface) ---------
./installer/linux/bundle.sh "$BUILD" "$APPDIR" >/dev/null
rm -f "$APPDIR/install.sh"        # inutile dans un paquet géré par apt

# --- Icônes dans le thème hicolor -----------------------------------------
# Le nom de fichier « lami.png » est ce que cherchent le lanceur et la barre
# des tâches (Icon=lami + StartupWMClass=lami).
#
# ATTENTION : un thème d'icônes exige que le fichier d'un dossier <N>x<N>
# mesure RÉELLEMENT N pixels. Y déposer une copie du 256x256 produit des
# entrées incohérentes que GTK écarte — d'où une icône générique. On ne
# génère donc une taille que si l'on sait vraiment redimensionner.
SRC_ICON="web/assets/lami-icon.png"
resize_icon() {   # <source> <taille> <destination> -> 0 si réellement redimensionné
    if command -v convert >/dev/null 2>&1; then
        convert "$1" -resize "${2}x${2}" "$3" 2>/dev/null && return 0
    fi
    python3 - "$1" "$2" "$3" <<'PY' 2>/dev/null && return 0
import sys
import gi
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf
src, size, dest = sys.argv[1], int(sys.argv[2]), sys.argv[3]
pb = GdkPixbuf.Pixbuf.new_from_file(src)
pb.scale_simple(size, size, GdkPixbuf.InterpType.BILINEAR).savev(dest, "png", [], [])
PY
    return 1
}

mkdir -p "$ROOT/usr/share/icons/hicolor/256x256/apps"
cp "$SRC_ICON" "$ROOT/usr/share/icons/hicolor/256x256/apps/lami.png"   # taille native
for size in 128 64 48 32; do
    dir="$ROOT/usr/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    resize_icon "$SRC_ICON" "$size" "$dir/lami.png" || rmdir -p --ignore-fail-on-non-empty "$dir"
done

# --- Lanceur en ligne de commande -----------------------------------------
cat > "$ROOT/usr/bin/lami" <<'EOF'
#!/bin/sh
exec /opt/lami/lami_shell "$@"
EOF
chmod +x "$ROOT/usr/bin/lami"

# --- Entrée du menu d'applications ----------------------------------------
# StartupWMClass DOIT correspondre à g_set_prgname() du shell, sinon le bureau
# n'associe pas la fenêtre à cette entrée et affiche une icône générique.
cat > "$ROOT/usr/share/applications/lami.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LAMI
GenericName=Launcher Minecraft
Comment=Rejoins un serveur Minecraft moddé en une seule adresse
Exec=lami
TryExec=/opt/lami/lami_shell
Icon=lami
Terminal=false
Categories=Game;
StartupNotify=true
StartupWMClass=lami
Keywords=Minecraft;Launcher;Atraxe;jeu;
EOF

# --- Métadonnées pour le centre d'applications ----------------------------
cat > "$ROOT/usr/share/metainfo/fr.atraxe.lami.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>fr.atraxe.lami</id>
  <name>LAMI</name>
  <summary>Launcher Minecraft d'Atraxe</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>MIT</project_license>
  <description>
    <p>
      LAMI installe automatiquement la bonne version de Minecraft, le mod loader
      et les mods d'un serveur à partir de son adresse, puis lance le jeu.
    </p>
  </description>
  <launchable type="desktop-id">lami.desktop</launchable>
  <categories><category>Game</category></categories>
  <releases><release version="$VERSION"/></releases>
</component>
EOF

# --- Métadonnées du paquet ------------------------------------------------
INSTALLED_KB="$(du -sk "$ROOT" | cut -f1)"
cat > "$ROOT/DEBIAN/control" <<EOF
Package: lami
Version: $VERSION
Section: games
Priority: optional
Architecture: amd64
Depends: libwebkit2gtk-4.1-0 | libwebkit2gtk-4.0-37, libgtk-3-0, libglib2.0-0, libsecret-1-0
Installed-Size: $INSTALLED_KB
Maintainer: Gimaxe <gimaxe13579@gmail.com>
Homepage: https://github.com/Gimaxe/LAMI
Description: LAMI - Launcher Minecraft Atraxe
 Launcher Minecraft qui installe tout seul la version du jeu, le mod loader,
 les mods, resource packs et shaders d'un serveur a partir de son adresse.
 Les bibliotheques Qt necessaires sont fournies avec l'application.
EOF

# Rafraîchissement des caches du bureau après installation/désinstallation.
cat > "$ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
EOF
cat > "$ROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
EOF
chmod 755 "$ROOT/DEBIAN/postinst" "$ROOT/DEBIAN/postrm"

# Contrôle : chaque icône doit mesurer exactement la taille de son dossier,
# sinon le thème l'écarte et le bureau retombe sur une icône générique.
for f in $(find "$ROOT/usr/share/icons" -name lami.png); do
    expected="$(basename "$(dirname "$(dirname "$f")")" | cut -dx -f1)"
    real="$(python3 -c "
import struct,sys
d=open('$f','rb').read(); print(struct.unpack('>II', d[16:24])[0])" 2>/dev/null || echo 0)"
    if [ "$real" != "$expected" ]; then
        echo "ERREUR : $f mesure ${real}px au lieu de ${expected}px." >&2
        exit 1
    fi
done

DEB="$OUT/LAMI_${VERSION}_amd64.deb"
dpkg-deb --build --root-owner-group "$ROOT" "$DEB" >/dev/null
echo "Paquet construit : $DEB ($(du -h "$DEB" | cut -f1))"
