#!/bin/bash
# Starts a UWP app set up with setup-prefix.sh.
# Usage: scripts/run-app.sh [package-name] [seconds]   (the name can be left out if only one app is set up;
#        seconds is an optional time limit, for testing)
# Each run is logged to $LOGS/run-<time>.log (the last 8 are kept); a crash backtrace is saved next to it.
# Environment: WINE_CORE_WINDOWED=1 keeps the app in a window; WINEDEBUG overrides the logging channels.
set -uo pipefail
source "$(dirname "$0")/common.sh"
[ -x "$WINE_ROOT/bin/wine" ] || die "patched Wine not found in $WINE_ROOT: run scripts/fetch-release.sh or scripts/build-wine.sh"
shopt -s nullglob
apps=("$PREFIX/drive_c/$APPS_DIR_NAME"/*/)
[ ${#apps[@]} -gt 0 ] || die "no app set up: run scripts/setup-prefix.sh /path/to/package-folder"
NAME="${1:-}"
if [ -z "$NAME" ]; then
    [ ${#apps[@]} -eq 1 ] || die "several apps are set up, name one: $(for a in "${apps[@]}"; do basename "$a"; done | tr '\n' ' ')"
    NAME="$(basename "${apps[0]}")"
else
    shift
fi
APP="$PREFIX/drive_c/$APPS_DIR_NAME/$NAME"
[ -d "$APP" ] || die "app $NAME is not set up in $PREFIX"
IFS=$'\t' read -r _ EXE < <(python3 -I "$REPO/scripts/uwptool.py" info "$APP")

wine_env
export WINEDLLOVERRIDES="${UWP_DLLOVERRIDES:-mscoree,mshtml=d;dxgi=n;d3d12=n;d3d12core=n;wintypes=n;windows.storage=n}"
export WINEDEBUG="${WINEDEBUG:-err+all,fixme+all,+twinapi}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-warn}" DXVK_LOG_PATH="$LOGS"
[ -f "$REPO/apps/$NAME.conf" ] && source "$REPO/apps/$NAME.conf"
mkdir -p "$LOGS"
cd "$APP" || exit 1
rm -f "$PREFIX/drive_c/crash.txt"
LOG="$LOGS/run-$NAME-$(date +%Y%m%d-%H%M%S).log"
ln -sf "$LOG" "$LOGS/latest.log"
echo "Logging to $LOG"
if [ -n "${1:-}" ]; then timeout "$1" wine "$EXE" > "$LOG" 2>&1; else wine "$EXE" > "$LOG" 2>&1; fi
RC=$?
echo "=== process exited with status $RC at $(date +%T) ===" >> "$LOG"
[ -f "$PREFIX/drive_c/crash.txt" ] && cp "$PREFIX/drive_c/crash.txt" "${LOG%.log}.crash.txt"
ls -1t "$LOGS"/run-*.log 2>/dev/null | tail -n +9 | xargs -r rm -f
ls -1t "$LOGS"/run-*.crash.txt 2>/dev/null | tail -n +9 | xargs -r rm -f
exit $RC
