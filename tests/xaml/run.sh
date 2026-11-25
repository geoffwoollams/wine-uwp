#!/bin/bash
# Runs the XAML test apps (xamlhello: milestone 1, xamllayout: milestone 2, xamlmarkup: milestone 3) in a throwaway Wine prefix (never the shared one): tests/xaml/run.sh [keep-prefix-dir]
# Opens a 1280x720 window for a few seconds. Exit status 0 means every check passed.
# XAML_MS_XBF_DIR=dir also loads Microsoft's compiler-made .xbf test files from dir.
set -uo pipefail
source "$(dirname "$0")/../../scripts/common.sh"
[ -x "$WINE_ROOT/bin/wine" ] || die "patched Wine not found in $WINE_ROOT"
[ -f "$BUILD/tests/xaml/xamllayout.exe" ] || die "build the tests first: tests/xaml/build.sh"
TMP="${1:-$(mktemp -d)}"
mkdir -p "$TMP"
export WINEPREFIX="$TMP/prefix" WINEARCH=win64 PATH="$WINE_ROOT/bin:$PATH"
export WINEDLLOVERRIDES="mscoree,mshtml=d" WINE_CORE_WINDOWED=1 WINEDEBUG="${WINEDEBUG:-err+all,fixme+xaml}"
[ -d "$WINEPREFIX" ] || { info "Creating a test prefix in $WINEPREFIX"; wineboot -i >/dev/null 2>&1; wineserver -w; }
cd "$WINEPREFIX/drive_c" || exit 1
RC=0
cp "$BUILD"/tests/xaml/*.xbf .
cp "$BUILD"/tests/xaml/*.xaml .
# optional: Microsoft's compiler-made test files (microsoft-ui-xaml, dxaml/test/resources/res/...), to check the reader
if [ -n "${XAML_MS_XBF_DIR:-}" ]; then mkdir -p ms; cp "$XAML_MS_XBF_DIR"/*.xbf ms/; fi
for t in xamlhello xamllayout xamlmarkup; do
    info "$t"
    cp "$BUILD/tests/xaml/$t.exe" .
    timeout 60 wine $t.exe || RC=1
    wineserver -w
done
ls "$WINEPREFIX"/drive_c/*.bmp >/dev/null 2>&1 && info "screenshots in $WINEPREFIX/drive_c/"
[ $RC = 0 ] && info "all XAML tests passed" || info "some XAML tests FAILED"
exit $RC
