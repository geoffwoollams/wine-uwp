# Shared settings for the wine-uwp scripts. Every path can be overridden from the environment.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${UWP_BUILD_DIR:-$REPO/build}"                        # sources, objects and installed builds (also where a release unpacks)
WINE_ROOT="$BUILD/wine-root/usr/local"                       # patched Wine installed here (make install DESTDIR)
DXVK_DXGI="$BUILD/dxvk/dxgi.dll"                             # patched DXVK dxgi.dll
COMPONENTS="$BUILD/components"                               # d3d12, d3d12core, wintypes, windows.storage (from a release)
DATA="${UWP_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/wine-uwp}"
PREFIX="${UWP_PREFIX:-$DATA/prefix}"                         # the Wine prefix the apps run in
LOGS="${UWP_LOGS:-$DATA/logs}"
APPS_DIR_NAME="UWP"                                          # apps are linked into C:\UWP\<package name> inside the prefix

WINE_TAG="wine-11.22"
WINE_GIT="https://gitlab.winehq.org/wine/wine.git"
DXVK_COMMIT="f90f1bf180f41356695ddf3df6af62119fb64a3e"     # DXVK master the patch was made against (v3.1.1-235)
DXVK_GIT="https://github.com/doitsujin/dxvk.git"
GE_PROTON_VERSION="GE-Proton11-14"
RELEASE_REPO="${UWP_RELEASE_REPO:-geoffwoollams/wine-uwp}"   # GitHub repository whose releases fetch-release.sh downloads
RELEASE_ASSET="wine-uwp-x86_64.tar.xz"
COMPONENT_DLLS="d3d12.dll d3d12core.dll wintypes.dll windows.storage.dll"

die() { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

# Find the GE-Proton install that supplies vkd3d-proton and the extra WinRT DLLs (only needed when building from source).
find_ge_proton() {
    local d
    shopt -s nullglob
    for d in "${GE_PROTON:-}" \
             "$HOME"/.local/share/Steam/compatibilitytools.d/"$GE_PROTON_VERSION"{,-*} \
             "$HOME"/.steam/root/compatibilitytools.d/"$GE_PROTON_VERSION"{,-*} \
             "$HOME"/.var/app/com.valvesoftware.Steam/data/Steam/compatibilitytools.d/"$GE_PROTON_VERSION"{,-*} \
             "$HOME"/.local/share/lutris/runners/proton/"$GE_PROTON_VERSION"{,-*}; do
        [ -n "$d" ] && [ -f "$d/files/lib/wine/x86_64-windows/wintypes.dll" ] && { echo "$d"; return 0; }
    done
    return 1
}

# Print "<vkd3d-proton dir> <wine dll dir>", the directories holding the four component DLLs.
# A release provides them in $COMPONENTS; otherwise they come from GE-Proton.
find_components() {
    local ge
    if [ -f "$COMPONENTS/d3d12.dll" ] && [ -f "$COMPONENTS/wintypes.dll" ]; then echo "$COMPONENTS $COMPONENTS"; return 0; fi
    ge="$(find_ge_proton)" || return 1
    echo "$ge/files/lib/wine/vkd3d-proton/x86_64-windows $ge/files/lib/wine/x86_64-windows"
}

wine_env() {
    export WINEPREFIX="$PREFIX" WINEARCH=win64
    export PATH="$WINE_ROOT/bin:$PATH"
}
