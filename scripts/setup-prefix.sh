#!/bin/bash
# Creates the shared Wine prefix (once) and links one unpacked UWP package into it. The package folder is never modified.
# Usage: scripts/setup-prefix.sh /path/to/package-folder [--vclibs /path/to/Microsoft.VCLibs.140.00_..._x64.appx|folder] [--skip-verify]
# Run it again for each app; they share the prefix.
set -euo pipefail
source "$(dirname "$0")/common.sh"

PKG_DIR="" VCLIBS="" VERIFY=1
while [ $# -gt 0 ]; do
    case "$1" in
        --vclibs) VCLIBS="$2"; shift 2 ;;
        --skip-verify) VERIFY=0; shift ;;
        -*) die "unknown option $1" ;;
        *) PKG_DIR="$(cd "$1" && pwd)"; shift ;;
    esac
done
[ -n "$PKG_DIR" ] || die "usage: $0 /path/to/package-folder [--vclibs path] [--skip-verify]"
[ -x "$WINE_ROOT/bin/wine" ] || die "patched Wine not found in $WINE_ROOT: run scripts/fetch-release.sh or scripts/build-wine.sh first"
[ -f "$DXVK_DXGI" ] || die "patched dxgi.dll not found at $DXVK_DXGI: run scripts/fetch-release.sh or scripts/build-dxvk.sh first"
IFS=$'\t' read -r PKG_NAME PKG_EXE < <(python3 -I "$REPO/scripts/uwptool.py" info "$PKG_DIR")
# the manifest's Executable is a Windows path ("GearGame\Binaries\UWP64\GearGame.exe"); check it with / separators
[ -f "$PKG_DIR/${PKG_EXE//\\//}" ] || die "$PKG_EXE (from AppxManifest.xml) not found in $PKG_DIR"
read -r VKD3D_DIR WINEDLL_DIR < <(find_components) || die "d3d12/wintypes DLLs not found: run scripts/fetch-release.sh, or install $GE_PROTON_VERSION (see README) or set GE_PROTON=/path/to/$GE_PROTON_VERSION"
info "Package $PKG_NAME ($PKG_EXE); components from $VKD3D_DIR"

if [ -z "$VCLIBS" ]; then
    VCLIBS="$(ls -1 "$PKG_DIR"/redist/Microsoft.VCLibs.140.00_*_x64_*.appx "$PKG_DIR"/redist/Microsoft.VCLibs.140.00_*_x64_*.Appx 2>/dev/null | head -1 || true)"
    [ -n "$VCLIBS" ] || echo "warning: no x64 Microsoft.VCLibs.140.00 package found in $PKG_DIR/redist; C++/CX apps need one: pass it with --vclibs (see README)" >&2
fi

if [ "$VERIFY" = 1 ]; then
    info "Verifying files against AppxBlockMap.xml (a few minutes for a large app; --skip-verify to skip)"
    python3 -I "$REPO/scripts/uwptool.py" verify "$PKG_DIR" || die "package files do not match the package's hash list"
fi

wine_env
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=d"
if [ ! -f "$PREFIX/system.reg" ]; then
    info "Creating Wine prefix in $PREFIX"
    mkdir -p "$PREFIX"
    wineboot -i >/dev/null 2>&1
    wineserver -w
fi
SYS32="$PREFIX/drive_c/windows/system32"

info "Installing vkd3d-proton (D3D12), DXVK dxgi and the WinRT DLLs"
cp "$VKD3D_DIR/d3d12.dll" "$VKD3D_DIR/d3d12core.dll" "$SYS32/"
for dll in wintypes windows.storage; do   # Wine PE builds: copied so they load as native DLLs, overriding ours
    python3 -I "$REPO/scripts/uwptool.py" native-copy "$WINEDLL_DIR/$dll.dll" "$SYS32/$dll.dll"
done
cp "$DXVK_DXGI" "$SYS32/dxgi.dll"

APP="$PREFIX/drive_c/$APPS_DIR_NAME/$PKG_NAME"
info "Linking the package into C:\\$APPS_DIR_NAME\\$PKG_NAME"
mkdir -p "$APP"
find "$APP" -maxdepth 1 -type l -delete
for f in "$PKG_DIR"/*; do
    name="$(basename "$f")"
    [ -n "$VCLIBS" ] && case "${name,,}" in *_app.dll) continue ;; esac   # the VC++ runtime comes from the VCLibs package below
    ln -s "$f" "$APP/$name"
done
[ -n "$VCLIBS" ] && python3 -I "$REPO/scripts/uwptool.py" vclibs "$VCLIBS" "$APP"

info "Registering WinRT classes"
python3 -I "$REPO/scripts/uwptool.py" registry "$PKG_DIR" "C:\\$APPS_DIR_NAME\\$PKG_NAME" > "$PREFIX/drive_c/uwp-$PKG_NAME.reg"
wine regedit /S "C:\\uwp-$PKG_NAME.reg"
wineserver -w
info "Done. Start it with scripts/run-app.sh $PKG_NAME"
