#!/bin/bash
# Removes an app set up with setup-prefix.sh: what wine-uwp made for it, never the package folder it was set up from.
# Usage: scripts/remove-app.sh PACKAGE-NAME [--data] [--keep-package] [--yes]
#        scripts/remove-app.sh PACKAGE-NAME --slot SLOT [--data] [--yes]
# Removed: its folder of links in the prefix (C:\UWP\<package>), its WinRT class registrations, its record in
# ~/.local/share/wine-uwp/apps (and its per-user settings files), its launchers and launcher icon, and the copy unpacked
# from a package file (~/.local/share/wine-uwp/packages/<package>, and each version slot's own; --keep-package keeps them).
# Its saves and settings (AppData\Local\Packages\<family>, and every version slot's data folder) are deleted only with
# --data. The package folder it was set up from is left alone when it isn't one of wine-uwp's unpacked copies, such as a
# folder copied from Windows or a game installed with Lutris: its path is printed, for you to delete if you want to.
# --slot SLOT removes one version kept apart with setup-prefix.sh --data-slot instead (not the one set up now): its
# unpacked copy, and with --data its saves.
# Asks first on a terminal, unless --yes is given.
set -euo pipefail
source "$(dirname "$0")/common.sh"

NAME="" DATA_TOO=0 KEEP_PACKAGE=0 YES=0 SLOT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --data) DATA_TOO=1; shift ;;
        --keep-package) KEEP_PACKAGE=1; shift ;;
        --yes|-y) YES=1; shift ;;
        --slot) SLOT="$2"; shift 2 ;;
        -*) die "unknown option $1" ;;
        *) [ -z "$NAME" ] || die "one package name at a time"; NAME="$1"; shift ;;
    esac
done
[ -n "$NAME" ] || die "usage: $0 PACKAGE-NAME [--data] [--keep-package] [--yes], or $0 PACKAGE-NAME --slot SLOT [--data] [--yes]"
# package and slot names end up in paths: nothing that could leave the folders they belong in
for v in "$NAME" "$SLOT"; do
    [ -z "$v" ] || [[ "$v" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die "not a package or slot name: '$v'"
done

confirm() {
    [ "$YES" = 1 ] && return 0
    [ -t 0 ] || die "not asking without a terminal: add --yes"
    local answer
    read -r -p "$1 [y/N] " answer || return 1
    case "${answer,,}" in y|yes) return 0 ;; *) return 1 ;; esac
}

# an existing folder strictly inside another one, after resolving the path's parents (not the folder itself)
inside() {
    local path parent
    path="$(cd "$(dirname "$1")" 2>/dev/null && pwd)/$(basename "$1")" || return 1
    parent="$(cd "$2" 2>/dev/null && pwd)" || return 1
    [[ "$path" == "$parent"/?* ]]
}

remove_tree() {   # a folder of wine-uwp's own; rm -r doesn't follow the links in it
    [ -e "$1" ] || [ -L "$1" ] || return 0
    if [ -L "$1" ]; then rm -f "$1"; else rm -rf --one-file-system "$1"; fi
    echo "  removed $1"
}

if [ -n "$SLOT" ]; then
    dir="$VERSIONS/$NAME/$SLOT"
    [ -d "$dir" ] || die "$NAME has no version slot $SLOT"
    [ "$(app_slot "$NAME")" = "$SLOT" ] && die "$SLOT is the version of $NAME set up now: switch to another one first (setup-prefix.sh --switch), or remove the app"
    what="the unpacked copy of $NAME $SLOT"
    [ "$DATA_TOO" = 1 ] && what="$what and its saves"
    confirm "Remove $what?" || { echo "Nothing removed."; exit 1; }
    src="$(head -1 "$dir/source" 2>/dev/null || true)"
    remove_tree "$dir/$NAME"
    rm -f "$dir/source"
    [ "$DATA_TOO" = 1 ] && remove_tree "$dir/data"
    rm -f "$SOURCES/$NAME@$SLOT.env"
    rmdir "$dir" 2>/dev/null || true
    rmdir "$VERSIONS/$NAME" 2>/dev/null || true
    if [ -n "$src" ] && [ "$src" != "$dir/$NAME" ] && [ -d "$src" ]; then
        info "Left alone: the package folder $src"
    fi
    [ -d "$dir" ] && info "Kept its saves in $dir/data"
    info "Removed version $SLOT of $NAME"
    exit 0
fi

PREFIX="$(app_prefix "$NAME")"
APP="$PREFIX/drive_c/$APPS_DIR_NAME/$NAME"
[ -d "$APP" ] || [ -f "$SOURCES/$NAME.source" ] || die "$NAME is not set up (see scripts/list-apps.sh)"
SRC="$(sed -n 1p "$SOURCES/$NAME.source" 2>/dev/null || true)"
FAMILY=""
for manifest in "$APP/AppxManifest.xml" "$SRC/AppxManifest.xml"; do
    if [ -f "$manifest" ]; then FAMILY="$(python3 -I "$REPO/scripts/uwptool.py" family "$(dirname "$manifest")")"; break; fi
done
PKG_DATA=""
[ -n "$FAMILY" ] && PKG_DATA="$PREFIX/drive_c/users/$(prefix_user)/AppData/Local/Packages/$FAMILY"

summary="Remove $NAME"
[ "$DATA_TOO" = 1 ] && summary="$summary and delete its saves" || summary="$summary (its saves are kept)"
confirm "$summary?" || { echo "Nothing removed."; exit 1; }

info "Removing $NAME"
[ -L "$APP" ] && die "$APP is a link, not wine-uwp's folder of links: not touching it"
if [ -d "$APP" ]; then
    inside "$APP" "$PREFIX/drive_c/$APPS_DIR_NAME" || die "$APP is not in the prefix's $APPS_DIR_NAME folder"
    remove_tree "$APP"
fi

# its own WinRT classes, registered under its package name
REG="$PREFIX/drive_c/uwp-$NAME.reg"
if [ -f "$REG" ]; then
    if [ -x "$WINE_ROOT/bin/wine" ] && [ -f "$PREFIX/system.reg" ]; then
        python3 -I "$REPO/scripts/uwptool.py" unregister "$REG" > "$PREFIX/drive_c/uwp-$NAME-remove.reg"
        (PREFIX="$PREFIX"; wine_env; export WINEDEBUG=-all
         wine regedit /S "C:\\uwp-$NAME-remove.reg" && wineserver -w) || echo "warning: could not remove its registry entries" >&2
        rm -f "$PREFIX/drive_c/uwp-$NAME-remove.reg"
        echo "  removed its WinRT class registrations"
    fi
    rm -f "$REG"
fi

# launchers
DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
for f in "$APPLICATIONS/wine-uwp-$NAME.desktop" "$DESKTOP_DIR/wine-uwp-$NAME.desktop" "$DATA/icons/$NAME.png"; do
    [ -f "$f" ] && rm -f "$f" && echo "  removed $f"
done
command -v update-desktop-database >/dev/null && update-desktop-database "$APPLICATIONS" >/dev/null 2>&1 || true

# wine-uwp's own unpacked copies
if [ "$KEEP_PACKAGE" = 0 ]; then
    remove_tree "$PACKAGES/$NAME"
    for dir in "$VERSIONS/$NAME"/*/; do
        [ -d "$dir" ] && remove_tree "${dir%/}/$NAME"
    done
fi

# saves
if [ "$DATA_TOO" = 1 ]; then
    if [ -n "$PKG_DATA" ] && { [ -e "$PKG_DATA" ] || [ -L "$PKG_DATA" ]; }; then
        inside "$PKG_DATA" "$PREFIX/drive_c/users" || die "$PKG_DATA is not in the prefix's users folder"
        remove_tree "$PKG_DATA"
    fi
    for dir in "$VERSIONS/$NAME"/*/; do
        [ -d "$dir" ] && remove_tree "${dir%/}/data"
    done
fi
for dir in "$VERSIONS/$NAME"/*/; do
    [ -d "$dir" ] || continue
    rm -f "${dir%/}/source"
    rmdir "$dir" 2>/dev/null || true
done
rmdir "$VERSIONS/$NAME" 2>/dev/null || true

rm -f "$SOURCES/$NAME.source" "$SOURCES/$NAME.env" "$SOURCES/$NAME"@*.env
if [ -n "$SRC" ] && [ -d "$SRC" ] && [ "$SRC" != "$PACKAGES/$NAME" ] && [[ "$SRC" != "$VERSIONS/$NAME/"* ]]; then
    info "Left alone: the package folder it was set up from, $SRC"
fi
if [ "$DATA_TOO" = 0 ]; then
    [ -n "$PKG_DATA" ] && [ -e "$PKG_DATA" ] && info "Kept its saves in $PKG_DATA"
    [ -d "$VERSIONS/$NAME" ] && info "Kept the saves of its versions in $VERSIONS/$NAME"
fi
info "Removed $NAME"
