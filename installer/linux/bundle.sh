#!/bin/bash
# Fabrique un paquet Linux AUTONOME : les bibliothèques Qt (et le greffon TLS,
# indispensable au HTTPS) sont embarquées dans lib/ et plugins/, de sorte que
# LAMI fonctionne sur une machine où Qt 6 n'est pas installé.
#
#   usage : installer/linux/bundle.sh <dossier-build> <dossier-dist>
#
# Sans ça, seul lami_shell démarre (il ne dépend pas de Qt) : la fenêtre s'ouvre
# mais le moteur meurt aussitôt → « Backend non connecté ».
set -euo pipefail

BUILD="${1:-build}"
DIST="${2:-dist}"

mkdir -p "$DIST/lib" "$DIST/plugins/tls"

# --- Binaires et interface ------------------------------------------------
cp "$BUILD/lami_backend" "$BUILD/lami_shell" "$DIST/"
cp -r web "$DIST/web"
cp installer/linux/install.sh "$DIST/install.sh"
chmod +x "$DIST/install.sh" "$DIST/lami_backend" "$DIST/lami_shell"

# Indique à Qt où trouver ses greffons (chemin relatif à l'exécutable).
cat > "$DIST/qt.conf" <<'EOF'
[Paths]
Plugins = plugins
EOF

# --- Greffon TLS : sans lui, tout HTTPS échoue silencieusement -------------
TLS_PLUGIN="$(find /usr/lib /usr/lib64 -name 'libqopensslbackend.so' -path '*tls*' 2>/dev/null | head -n1 || true)"
if [ -n "$TLS_PLUGIN" ]; then
    cp "$TLS_PLUGIN" "$DIST/plugins/tls/"
else
    echo "ATTENTION : greffon TLS Qt introuvable — les téléchargements HTTPS échoueraient." >&2
fi

# --- Bibliothèques : dépendances de tout ce qui est embarqué --------------
# On copie les bibliothèques applicatives et on laisse au système celles qui
# font partie de sa base (glibc, libstdc++, pilotes graphiques…) : les embarquer
# provoque plus d'incompatibilités qu'elle n'en résout.
is_system_lib() {
    case "$1" in
        libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|ld-linux*|\
        libgcc_s.so.*|libstdc++.so.*|libresolv.so.*|libutil.so.*|\
        libX*|libGL*|libEGL*|libgbm*|libdrm*|libwayland*|libxkbcommon*|\
        libglib-2.0.so.*|libgobject-2.0.so.*|libgio-2.0.so.*|libgmodule-2.0.so.*)
            return 0 ;;
    esac
    return 1
}

collect_deps() {
    ldd "$1" 2>/dev/null | awk '/=> \// {print $3}' | sort -u
}

# File d'attente : binaire + greffon TLS, puis leurs dépendances transitives.
QUEUE=("$DIST/lami_backend")
[ -n "$TLS_PLUGIN" ] && QUEUE+=("$DIST/plugins/tls/libqopensslbackend.so")

declare -A SEEN=()
while [ ${#QUEUE[@]} -gt 0 ]; do
    current="${QUEUE[0]}"; QUEUE=("${QUEUE[@]:1}")
    while read -r lib; do
        [ -z "$lib" ] && continue
        base="$(basename "$lib")"
        [ -n "${SEEN[$base]:-}" ] && continue
        SEEN[$base]=1
        is_system_lib "$base" && continue
        cp -L "$lib" "$DIST/lib/"
        QUEUE+=("$DIST/lib/$base")
    done < <(collect_deps "$current")
done

# --- OpenSSL : chargé À LA DEMANDE (dlopen) par le greffon TLS, donc invisible
# pour ldd et jamais copié par la collecte ci-dessus. Sans lui, aucun
# téléchargement HTTPS ne fonctionne sur une machine sans OpenSSL.
for soname in libssl.so.3 libcrypto.so.3; do
    src="$(ldconfig -p 2>/dev/null | awk -v n="$soname" '$1==n {print $NF; exit}')"
    [ -n "$src" ] && [ ! -f "$DIST/lib/$soname" ] && cp -L "$src" "$DIST/lib/"
done

# --- Contrôles : le paquet doit être réellement autonome -------------------
# 1) Aucune dépendance non résolue (une lib absente de la machine de build
#    n'aurait PAS été copiée : ldd ne donne pas de chemin pour un « not found »).
if ldd "$DIST/lami_backend" 2>/dev/null | grep -q 'not found'; then
    echo "ERREUR : dépendances non résolues sur la machine de build :" >&2
    ldd "$DIST/lami_backend" | grep 'not found' >&2
    exit 1
fi
# 2) RPATH et non RUNPATH : seul le RPATH est hérité par les dépendances
#    transitives. Avec un RUNPATH, les libs DE Qt (libb2, icu…) seraient
#    cherchées sur le système et le moteur ne démarrerait pas sans Qt installé.
if ! readelf -d "$DIST/lami_backend" | grep -q '(RPATH)'; then
    echo "ERREUR : lami_backend n'a pas de RPATH (RUNPATH non hérité)." >&2
    readelf -d "$DIST/lami_backend" | grep -E 'RPATH|RUNPATH' >&2
    exit 1
fi
# 3) Les dépendances de Qt doivent être résolues DANS le paquet.
for lib in libb2.so.1 libicuuc.so libssl.so; do
    if ! ls "$DIST/lib/" | grep -q "^${lib%%.so*}"; then
        echo "AVERTISSEMENT : ${lib} absente du paquet." >&2
    fi
done

echo "Paquet autonome prêt : $(ls "$DIST/lib" | wc -l) bibliothèque(s) embarquée(s)."
ls "$DIST/lib" | sed 's/^/  /'
