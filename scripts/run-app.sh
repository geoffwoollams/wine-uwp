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
