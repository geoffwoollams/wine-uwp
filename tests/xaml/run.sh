#!/bin/bash
# Runs the XAML test apps in a throwaway Wine prefix (never the shared one): tests/xaml/run.sh [keep-prefix-dir]
# Opens a 1280x720 window for a few seconds. Exit status 0 means every check passed.
set -uo pipefail
source "$(dirname "$0")/../../scripts/common.sh"
[ -x "$WINE_ROOT/bin/wine" ] || die "patched Wine not found in $WINE_ROOT"
[ -f "$BUILD/tests/xaml/xamlhello.exe" ] || die "build the test first: tests/xaml/build.sh"
TMP="${1:-$(mktemp -d)}"
export WINEPREFIX="$TMP/prefix" WINEARCH=win64 PATH="$WINE_ROOT/bin:$PATH"
export WINEDLLOVERRIDES="mscoree,mshtml=d" WINE_CORE_WINDOWED=1 WINEDEBUG="${WINEDEBUG:-err+all,fixme+xaml}"
[ -d "$WINEPREFIX" ] || { info "Creating a test prefix in $WINEPREFIX"; wineboot -i >/dev/null 2>&1; wineserver -w; }
cp "$BUILD/tests/xaml/xamlhello.exe" "$WINEPREFIX/drive_c/"
cd "$WINEPREFIX/drive_c" || exit 1
timeout 60 wine xamlhello.exe
RC=$?
wineserver -w
[ -f xamlhello.bmp ] && info "screenshot: $WINEPREFIX/drive_c/xamlhello.bmp"
exit $RC
