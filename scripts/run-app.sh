#!/bin/bash
# Starts a UWP app set up with setup-prefix.sh, or sets one up first.
# Usage: scripts/run-app.sh [package-name | package-folder | package.appx] [seconds]
#        Without a name, the only app set up starts, or a numbered list of them asks which. Given a package folder or
#        an .appx/.msix (or bundle), the app is set up with setup-prefix.sh the first time (a package file is unpacked
#        into ~/.local/share/wine-uwp/packages/), then started. seconds is an optional time limit, for testing.
# Each run is logged to $LOGS/run-<time>.log (the last 8 are kept); a crash backtrace is saved next to it.
# Environment: WINE_CORE_WINDOWED=1 keeps the app in a window; WINEDEBUG overrides the logging channels.
set -uo pipefail
source "$(dirname "$0")/common.sh"
[ -x "$WINE_ROOT/bin/wine" ] || die "patched Wine not found in $WINE_ROOT: run scripts/fetch-release.sh or scripts/build-wine.sh"
shopt -s nullglob
NAME="${1:-}"
# a package folder or file: set it up unless it already is, from that same folder
if [ -n "$NAME" ] && { [ -f "$NAME" ] || [ -f "$NAME/AppxManifest.xml" ]; }; then
    PKG_DIR="$(cd "$(dirname "$NAME")" && pwd)/$(basename "$NAME")"
    if [ -f "$PKG_DIR" ]; then
        PKG_DIR="$(python3 -I "$REPO/scripts/uwptool.py" extract-package "$PKG_DIR" "$PACKAGES")" || die "could not unpack $NAME"
    fi
    PKG_DIR="$(cd "$PKG_DIR" && pwd)"
    IFS=$'\t' read -r PKG_NAME _ < <(python3 -I "$REPO/scripts/uwptool.py" info "$PKG_DIR") || die "$NAME is not a UWP package"
    if [ "$(head -1 "$SOURCES/$PKG_NAME.source" 2>/dev/null)" != "$PKG_DIR" ] ||
       [ ! -d "$(app_prefix "$PKG_NAME")/drive_c/$APPS_DIR_NAME/$PKG_NAME" ]; then
        info "Setting up $PKG_NAME from $PKG_DIR"
        "$REPO/scripts/setup-prefix.sh" "$PKG_DIR" || die "setting up $PKG_NAME failed"
    fi
    shift
    set -- "$PKG_NAME" "$@"
    NAME="$PKG_NAME"
fi
mapfile -t apps < <(list_apps | sort -u)
[ ${#apps[@]} -gt 0 ] || die "no app set up: run scripts/run-app.sh /path/to/package-folder-or.appx"
if [ -z "$NAME" ] && [ ${#apps[@]} -eq 1 ]; then
    NAME="${apps[0]}"
elif [ -z "$NAME" ]; then
    # several apps: number them with their display names and ask which to start
    for i in "${!apps[@]}"; do
        nice="$(python3 -I "$REPO/scripts/uwptool.py" appinfo "$(app_prefix "${apps[i]}")/drive_c/$APPS_DIR_NAME/${apps[i]}" 2>/dev/null | cut -f1)"
        if [ -n "$nice" ] && [ "$nice" != "${apps[i]}" ]; then printf '%3d) %s (%s)\n' $((i + 1)) "$nice" "${apps[i]}" >&2
        else printf '%3d) %s\n' $((i + 1)) "${apps[i]}" >&2; fi
    done
    [ -t 0 ] || die "several apps are set up: name one (scripts/run-app.sh <package-name>)"
    read -r -p "Start which app? [1-${#apps[@]}] " choice || exit 1
    [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le ${#apps[@]} ] || die "no app numbered '$choice'"
    NAME="${apps[choice - 1]}"
else
    shift
fi
PREFIX="$(app_prefix "$NAME")"
APP="$PREFIX/drive_c/$APPS_DIR_NAME/$NAME"
[ -d "$APP" ] || die "app $NAME is not set up in $PREFIX"
IFS=$'\t' read -r _ EXE < <(python3 -I "$REPO/scripts/uwptool.py" info "$APP")

wine_env
export WINEDLLOVERRIDES="${UWP_DLLOVERRIDES:-mscoree,mshtml=d;dxgi=n;d3d11=n;d3d10core=n;d3d12=n;d3d12core=n;wintypes=n;windows.storage=n}"
export WINEDEBUG="${WINEDEBUG:-err+all,fixme+all,+twinapi}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-warn}" DXVK_LOG_PATH="$LOGS"
[ -f "$REPO/apps/$NAME.conf" ] && source "$REPO/apps/$NAME.conf"
mkdir -p "$LOGS"
cd "$APP" || exit 1
rm -f "$PREFIX/drive_c/crash.txt"
LOG="$LOGS/run-$NAME-$(date +%Y%m%d-%H%M%S).log"
ln -sf "$LOG" "$LOGS/latest.log"
echo "Logging to $LOG"
# Everything appends: wineserver inherits this handle and keeps writing after the app exits (a development build of
# Wine lists its remaining objects when it shuts down), which would otherwise overwrite the lines after its position.
: > "$LOG"
if [ -n "${1:-}" ]; then timeout "$1" wine "$EXE" >> "$LOG" 2>&1; else wine "$EXE" >> "$LOG" 2>&1; fi
RC=$?
wineserver -w   # let the prefix shut down first, so the exit line is the log's last
echo "=== process exited with status $RC at $(date +%T) ===" >> "$LOG"
[ -f "$PREFIX/drive_c/crash.txt" ] && cp "$PREFIX/drive_c/crash.txt" "${LOG%.log}.crash.txt"
# keep the newest 8 logs and backtraces. Not "ls -1t pattern": with nullglob (set above), a pattern matching nothing
# leaves a bare ls of the current directory, the app's folder, and that removed its package links.
prune() { find "$LOGS" -maxdepth 1 -type f -name "$1" -printf '%T@ %p\n' | sort -rn | tail -n +9 | cut -d' ' -f2- | xargs -r -d '\n' rm -f --; }
prune 'run-*.log'
prune 'run-*.crash.txt'
exit $RC
