#!/bin/bash
# Creates the shared Wine prefix (once) and links one unpacked UWP package into it. The package folder is never modified.
# An .appx/.msix file (or .appxbundle/.msixbundle) is first unpacked into ~/.local/share/wine-uwp/packages/<package name>.
# Usage: scripts/setup-prefix.sh /path/to/package-folder|package.appx [--frameworks DIR] [--vclibs /path/to/Microsoft.VCLibs...appx|folder]
#        [--verify] [--shortcut | --no-shortcut] [--prefix DIR] [--no-download]
#        scripts/setup-prefix.sh --update-all
# Run it again for each app; they share the prefix. It offers to add a launcher (a .desktop file with the app's icon) to
# the applications menu and the desktop; --shortcut adds it without asking, --no-shortcut skips it.
# --prefix DIR uses another Wine prefix for this app instead of the shared one (it is remembered for later runs).
# --frameworks DIR imports the framework packages an app may depend on (Microsoft.VCLibs, Microsoft.NET.Native.*,
# Microsoft.UI.Xaml ...) into the shared store ~/.local/share/wine-uwp/frameworks, once: DIR is an unpacked package folder,
# an .appx/.msix, or a folder of them (copied from C:\Program Files\WindowsApps on Windows). --vclibs does the same for
# one package, and framework .appx files in the app's redist folder are imported too. Each app gets the newest stored
# version of every framework its manifest depends on, linked into its folder. Frameworks it needs that the store lacks are
# downloaded from Microsoft when there is a public copy (VC++ runtimes, .NET Native 2.2, WinUI 2.8; see uwptool.py
# FRAMEWORK_DOWNLOADS, each checked against its SHA-256); --no-download turns that off.
# --update-all sets up every app already in the prefix again, from the folders they were set up from (without the file
# check or the launcher question): what an upgraded Wine build needs, and what fetch-release.sh runs.
set -euo pipefail
source "$(dirname "$0")/common.sh"

PKG_DIR="" VCLIBS="" FRAMEWORK_SRC=() VERIFY=0 SHORTCUT=ask UPDATE_ALL=0 OWN_PREFIX="" DOWNLOAD=1
while [ $# -gt 0 ]; do
    case "$1" in
        --vclibs) VCLIBS="$2"; shift 2 ;;
        --frameworks) FRAMEWORK_SRC+=("$2"); shift 2 ;;
        --verify) VERIFY=1; shift ;;      # check every file against AppxBlockMap.xml first
        --skip-verify) VERIFY=0; shift ;; # the default now; still accepted
        --shortcut) SHORTCUT=yes; shift ;;
        --no-shortcut) SHORTCUT=no; shift ;;
        --update-all) UPDATE_ALL=1; shift ;;
        --no-download) DOWNLOAD=0; shift ;;
        --prefix) mkdir -p "$2"; OWN_PREFIX="$(cd "$2" && pwd)"; shift 2 ;;
        -*) die "unknown option $1" ;;
        *) if [ -f "$1" ]; then PKG_DIR="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; else PKG_DIR="$(cd "$1" && pwd)"; fi; shift ;;
    esac
done
if [ "$UPDATE_ALL" = 1 ]; then
    mapfile -t names < <(list_apps | sort -u)
    [ ${#names[@]} -gt 0 ] || { info "No apps set up yet"; exit 0; }
    failed=0
    for name in "${names[@]}"; do
        app="$(app_prefix "$name")/drive_c/$APPS_DIR_NAME/$name" src="" vcl=""
        if [ -f "$SOURCES/$name.source" ]; then
            { read -r src; read -r vcl || true; } < "$SOURCES/$name.source"
        elif [ -L "$app/AppxManifest.xml" ]; then
            src="$(dirname "$(readlink "$app/AppxManifest.xml")")"   # set up before the folder was recorded
        fi
        if [ -z "$src" ] || [ ! -f "$src/AppxManifest.xml" ]; then
            echo "warning: can't find the package folder $name was set up from${src:+ ($src)}; run scripts/setup-prefix.sh /path/to/it" >&2
            failed=1
            continue
        fi
        info "Updating $name from $src"
        args=("$src" --no-shortcut)
        [ -n "$vcl" ] && [ -e "$vcl" ] && args+=(--vclibs "$vcl")
        "$0" "${args[@]}" || { echo "warning: updating $name failed" >&2; failed=1; }
    done
    exit $failed
fi
[ -n "$PKG_DIR" ] || die "usage: $0 /path/to/package-folder [--vclibs path] [--verify] [--shortcut | --no-shortcut], or $0 --update-all"
[ -x "$WINE_ROOT/bin/wine" ] || die "patched Wine not found in $WINE_ROOT: run scripts/fetch-release.sh or scripts/build-wine.sh first"
[ -f "$DXVK_DXGI" ] || die "patched dxgi.dll not found at $DXVK_DXGI: run scripts/fetch-release.sh or scripts/build-dxvk.sh first"
if [ -f "$PKG_DIR" ]; then
    # a package file: unpack it once into the packages folder (again when it is a different version), and set up from there
    PKG_DIR="$(python3 -I "$REPO/scripts/uwptool.py" extract-package "$PKG_DIR" "$PACKAGES")" || die "could not unpack the package"
fi
IFS=$'\t' read -r PKG_NAME PKG_EXE < <(python3 -I "$REPO/scripts/uwptool.py" info "$PKG_DIR")
# the manifest's Executable is a Windows path ("GearGame\Binaries\UWP64\GearGame.exe"), and its case need not match the
# file's (Forza Motorsport 7 names ForzaMotorsport7.exe, the file is forzamotorsport7.exe): Windows and Wine ignore it
python3 -I "$REPO/scripts/uwptool.py" resolve "$PKG_DIR" "$PKG_EXE" >/dev/null || die "$PKG_EXE (from AppxManifest.xml) not found in $PKG_DIR"
read -r VKD3D_DIR WINEDLL_DIR < <(find_components) || die "d3d12/wintypes DLLs not found: run scripts/fetch-release.sh, or install $GE_PROTON_VERSION (see README) or set GE_PROTON=/path/to/$GE_PROTON_VERSION"
info "Package $PKG_NAME ($PKG_EXE); components from $VKD3D_DIR"

# its own prefix: the one given now, or the one recorded when it was set up before
[ -z "$OWN_PREFIX" ] && [ -f "$SOURCES/$PKG_NAME.source" ] && OWN_PREFIX="$(sed -n 3p "$SOURCES/$PKG_NAME.source")"
if [ -n "$OWN_PREFIX" ]; then
    PREFIX="$OWN_PREFIX"
    info "Using the prefix $PREFIX for this app"
    if [ -f "$PREFIX/system.reg" ] && [ ! -f "$PREFIX/.wine-uwp" ]; then
        echo "warning: $PREFIX was made by another Wine. wine-uwp's Wine upgrades it on first use and this replaces the" \
             "dxgi, d3d12 and WinRT DLLs in its system32, so the tool that made it may not run it properly afterwards." >&2
    fi
fi

# framework packages into the shared store: given ones, and any the package carries in redist/
[ -n "$VCLIBS" ] && FRAMEWORK_SRC+=("$VCLIBS")
[ -d "$PKG_DIR/redist" ] && FRAMEWORK_SRC+=("$PKG_DIR/redist")
for src in "${FRAMEWORK_SRC[@]}"; do
    [ -e "$src" ] || die "framework source $src not found"
    info "Importing framework packages from $src"
    python3 -I "$REPO/scripts/uwptool.py" import-frameworks "$src" "$FRAMEWORKS" | sed 's/^/  /'
done

if [ "$VERIFY" = 1 ]; then
    info "Verifying files against AppxBlockMap.xml (a few minutes for a large app)"
    python3 -I "$REPO/scripts/uwptool.py" verify "$PKG_DIR" || die "package files do not match the package's hash list"
fi

wine_env
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=d"
if [ ! -f "$PREFIX/system.reg" ]; then
    info "Creating Wine prefix in $PREFIX"
    mkdir -p "$PREFIX"
    wineboot -i >/dev/null 2>&1
    wineserver -w
    touch "$PREFIX/.wine-uwp"   # made by wine-uwp
fi
SYS32="$PREFIX/drive_c/windows/system32"

info "Installing vkd3d-proton (D3D12), DXVK dxgi and the WinRT DLLs"
cp "$VKD3D_DIR/d3d12.dll" "$VKD3D_DIR/d3d12core.dll" "$SYS32/"
# Wine PE builds from GE-Proton: windows.storage overrides ours as a native DLL; GE-Proton's wintypes only serves its
# DataReader, registered to it under another name (uwptool.py WINE_CLASSES), so everything else uses our wintypes
python3 -I "$REPO/scripts/uwptool.py" native-copy "$WINEDLL_DIR/windows.storage.dll" "$SYS32/windows.storage.dll"
python3 -I "$REPO/scripts/uwptool.py" native-copy "$WINEDLL_DIR/wintypes.dll" "$SYS32/uwp-wintypes.dll"
cp "$WINE_ROOT/lib/wine/x86_64-windows/wintypes.dll" "$SYS32/wintypes.dll"   # replaces the native copy older setups made
cp "$DXVK_DXGI" "$SYS32/dxgi.dll"
# D3D11 (Unity) and Direct2D (XAML) need DXVK's d3d11 and d3d10core with its dxgi; older releases don't have them
for dll in "$DXVK_D3D11" "$DXVK_D3D10CORE"; do
    if [ -f "$dll" ]; then cp "$dll" "$SYS32/"; else echo "warning: no $dll: D3D11 and XAML apps will fail (run scripts/build-dxvk.sh)" >&2; fi
done
# a prefix made by an older build lacks classes our patches register (msxml6: FreeThreadedXMLHTTP60, for Xbox Live)
wine regsvr32 /s msxml6.dll >/dev/null 2>&1 || echo "warning: could not register msxml6.dll" >&2
# Controllers: games see a pad only if it is an XInput (Xbox-style) gamepad. Wine's SDL backend presents every
# controller SDL knows that way, but takes Sony (054C: DualShock 4, DualSense) and Nintendo (057E: Switch Pro,
# Joy-Con) pads through raw hidraw instead, where they stay plain HID devices. Send them through SDL too, unless the
# prefix already says otherwise (set Hidraw to 1 there to get the raw device back).
for vid in 054C 057E; do
    key="HKLM\\System\\CurrentControlSet\\Services\\WineBus\\Devices\\$vid"
    wine reg query "$key" /v Hidraw >/dev/null 2>&1 || wine reg add "$key" /v Hidraw /t REG_DWORD /d 0 /f >/dev/null 2>&1 ||
        echo "warning: could not set up $vid controllers (WineBus\\Devices\\$vid)" >&2
done

APP="$PREFIX/drive_c/$APPS_DIR_NAME/$PKG_NAME"
info "Linking the package into C:\\$APPS_DIR_NAME\\$PKG_NAME"
mkdir -p "$APP"
find "$APP" -maxdepth 1 -type f -iname '*_app.dll' -delete   # the VC++ runtime an earlier version copied in
python3 -I "$REPO/scripts/uwptool.py" link-package "$PKG_DIR" "$APP" | sed 's/^/  /'
# frameworks it needs that the store lacks, from Microsoft's public downloads
if [ "$DOWNLOAD" = 1 ]; then
    while IFS=$'\t' read -r state name detail; do
        case "$state" in
            DOWNLOADED) info "Downloaded the framework package $name $detail" ;;
            FAILED) echo "warning: couldn't download the framework package $name: $detail" >&2 ;;
        esac
    done < <(python3 -I "$REPO/scripts/uwptool.py" download-frameworks "$PKG_DIR" "$FRAMEWORKS")
fi
# the frameworks it depends on, from the store
while IFS=$'\t' read -r state name version; do
    case "$state" in
        LINKED) info "Framework $name" ;;
        MISSING) case "$name" in
                     mrt100.dll*) echo "warning: $PKG_NAME needs mrt100.dll, which has no public download: copy it from" \
                                       "C:\\Windows\\System32 on a Windows PC and pass it with --frameworks (see README)" >&2 ;;
                     *) echo "warning: $PKG_NAME needs the framework package $name (version $version or later), which isn't" \
                             "imported: copy it from C:\\Program Files\\WindowsApps on a Windows PC and pass it with" \
                             "--frameworks (see README)" >&2 ;;
                 esac
                 ;;
    esac
done < <(python3 -I "$REPO/scripts/uwptool.py" link-frameworks "$PKG_DIR" "$FRAMEWORKS" "$APP")
mkdir -p "$SOURCES"
printf '%s\n%s\n%s\n' "$PKG_DIR" "" "$OWN_PREFIX" > "$SOURCES/$PKG_NAME.source"

info "Registering WinRT classes"
python3 -I "$REPO/scripts/uwptool.py" registry "$PKG_DIR" "C:\\$APPS_DIR_NAME\\$PKG_NAME" "$FRAMEWORKS" > "$PREFIX/drive_c/uwp-$PKG_NAME.reg"
wine regedit /S "C:\\uwp-$PKG_NAME.reg"
# time zones without their MUI names: IL2CPP apps' DateTime.Now fails on them (see uwptool.py timezone-mui)
wine regedit /E "C:\\uwp-timezones.reg" "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones"
python3 -I "$REPO/scripts/uwptool.py" timezone-mui "$PREFIX/drive_c/uwp-timezones.reg" > "$PREFIX/drive_c/uwp-timezones-mui.reg"
wine regedit /S "C:\\uwp-timezones-mui.reg"
rm -f "$PREFIX/drive_c/uwp-timezones.reg" "$PREFIX/drive_c/uwp-timezones-mui.reg"
wineserver -w

# A launcher: ~/.local/share/applications/wine-uwp-<package>.desktop (the menu) and a copy on the desktop, with the app's
# own icon. StartupWMClass is the name Wine gives the window (the lowercased executable, the X11 class and the Wayland
# app id), so the desktop puts the running window under the launcher's icon.
if [ "$SHORTCUT" = ask ]; then
    if [ -t 0 ] && [ -t 1 ]; then
        read -r -p "Add a launcher for this app to the applications menu and the desktop? [Y/n] " answer || answer=n
        case "${answer,,}" in n|no) SHORTCUT=no ;; *) SHORTCUT=yes ;; esac
    else
        SHORTCUT=no
    fi
fi
if [ "$SHORTCUT" = yes ]; then
    IFS=$'\t' read -r APP_TITLE APP_ICON < <(python3 -I "$REPO/scripts/uwptool.py" appinfo "$PKG_DIR")
    APPLICATIONS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
    ID="wine-uwp-$PKG_NAME"
    mkdir -p "$APPLICATIONS" "$DATA/icons"
    ICON_LINE=""
    if [ -n "$APP_ICON" ]; then cp "$APP_ICON" "$DATA/icons/$PKG_NAME.png" && ICON_LINE="Icon=$DATA/icons/$PKG_NAME.png"; fi
    exe_name="${PKG_EXE//\\//}"; exe_name="${exe_name##*/}"
    # Exec: quoted per the desktop entry spec; settings that move the prefix or data are passed on
    quote() { local v="${1//\\/\\\\}"; v="${v//\"/\\\"}"; v="${v//\$/\\\$}"; v="${v//\`/\\\`}"; printf '"%s"' "$v"; }
    EXEC="$(quote "$REPO/scripts/run-app.sh") $(quote "$PKG_NAME")"
    for var in UWP_PREFIX UWP_DATA_DIR UWP_LOGS UWP_BUILD_DIR; do
        [ -n "${!var:-}" ] && EXEC="env $var=$(quote "${!var}") ${EXEC#env }"
    done
    cat > "$APPLICATIONS/$ID.desktop" <<EOT
[Desktop Entry]
Type=Application
Name=$APP_TITLE
Comment=$APP_TITLE (UWP app, run with wine-uwp)
Exec=$EXEC
${ICON_LINE}
Terminal=false
Categories=Game;
StartupWMClass=${exe_name,,}
EOT
    chmod +x "$APPLICATIONS/$ID.desktop"
    command -v update-desktop-database >/dev/null && update-desktop-database "$APPLICATIONS" >/dev/null 2>&1
    if [ -d "$DESKTOP_DIR" ]; then
        cp "$APPLICATIONS/$ID.desktop" "$DESKTOP_DIR/$ID.desktop"
        chmod +x "$DESKTOP_DIR/$ID.desktop"
        # GNOME only starts desktop launchers marked as trusted
        command -v gio >/dev/null && gio set "$DESKTOP_DIR/$ID.desktop" metadata::trusted true 2>/dev/null
    fi
    info "Added a launcher for $APP_TITLE: $APPLICATIONS/$ID.desktop$([ -d "$DESKTOP_DIR" ] && echo " and $DESKTOP_DIR/$ID.desktop")"
fi
info "Done. Start it with scripts/run-app.sh $PKG_NAME$([ "$SHORTCUT" = yes ] && echo ", or from the launcher")"
