# Shared settings for the wine-uwp scripts. Every path can be overridden from the environment.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${UWP_BUILD_DIR:-$REPO/build}"                        # sources, objects and installed builds (also where a release unpacks)
WINE_ROOT="$BUILD/wine-root/usr/local"                       # patched Wine installed here (make install DESTDIR)
DXVK_DXGI="$BUILD/dxvk/dxgi.dll"                             # patched DXVK dxgi.dll
DXVK_D3D11="$BUILD/dxvk/d3d11.dll"                           # DXVK d3d11.dll: Wine's d3d11 needs Wine's dxgi
DXVK_D3D10CORE="$BUILD/dxvk/d3d10core.dll"                   # and d3d10core.dll, which Wine's d2d1 (XAML drawing) uses
COMPONENTS="$BUILD/components"                               # d3d12, d3d12core, wintypes, windows.storage (from a release)
DATA="${UWP_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/wine-uwp}"
PREFIX="${UWP_PREFIX:-$DATA/prefix}"                         # the Wine prefix the apps run in
LOGS="${UWP_LOGS:-$DATA/logs}"
APPS_DIR_NAME="UWP"                                          # apps are linked into C:\UWP\<package name> inside the prefix
FRAMEWORKS="$DATA/frameworks"   # framework packages (VCLibs, .NET Native, WinUI ...), one copy for every app
SOURCES="$DATA/apps"   # <package>.source: the package folder, its VCLibs package and its own prefix (empty: the shared one)
PACKAGES="$DATA/packages"   # apps set up from .appx/.msix files, unpacked: one folder per package name
VERSIONS="$DATA/versions"   # <package>/<version slot>/: versions kept apart (setup-prefix.sh --data-slot): source, data/
APPLICATIONS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"   # where launchers go

WINE_TAG="wine-11.22"
UWP_VERSION="6"                                              # wine-uwp's own version, a whole number: raise it by one for each GitHub release
RELEASE_VERSION="${WINE_TAG#wine-}-uwp$UWP_VERSION"          # e.g. 11.22-uwp5: the release tag, and Wine's version as wine-11.22-uwp5
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

# The prefix an app was set up in: the one recorded for it with setup-prefix.sh --prefix, or the shared one.
app_prefix() {
    local p=""
    [ -f "$SOURCES/$1.source" ] && p="$(sed -n 3p "$SOURCES/$1.source")"
    echo "${p:-$PREFIX}"
}

# The version slot an app was set up in (setup-prefix.sh --data-slot), or nothing.
app_slot() {
    [ -f "$SOURCES/$1.source" ] && sed -n 4p "$SOURCES/$1.source"
    return 0
}

# The per-user settings file run-app.sh reads for an app: one per version slot when it has them.
app_settings_file() {
    local slot
    slot="$(app_slot "$1")"
    echo "$SOURCES/$1${slot:+@$slot}.env"
}

# The folder under drive_c/users the prefix's Windows user has: Wine names it after the Unix user.
prefix_user() { echo "${USER:-$(id -un)}"; }

# Every app set up: those in the shared prefix, and those recorded with a prefix of their own.
list_apps() {
    local a f p
    for a in "$PREFIX/drive_c/$APPS_DIR_NAME"/*/; do [ -d "$a" ] && basename "$a"; done
    for f in "$SOURCES"/*.source; do
        [ -f "$f" ] || continue
        p="$(sed -n 3p "$f")"
        [ -n "$p" ] && [ -d "$p/drive_c/$APPS_DIR_NAME/$(basename "$f" .source)" ] && basename "$f" .source
    done
}

wine_env() {
    export WINEPREFIX="$PREFIX" WINEARCH=win64
    export PATH="$WINE_ROOT/bin:$PATH"
}
