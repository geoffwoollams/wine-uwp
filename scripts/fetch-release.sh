#!/bin/bash
# Downloads the prebuilt Wine, dxgi.dll and WinRT/D3D12 DLLs from a GitHub release and unpacks them into build/,
# so you can skip build-wine.sh, build-dxvk.sh and installing GE-Proton. Also fetches the VC++ runtime framework package
# (Microsoft.VCLibs.140.00) into the framework store if it isn't there yet.
# Usage: scripts/fetch-release.sh [tag]      (default: the latest release)
# Apps already set up are then set up again for the new build (setup-prefix.sh --update-all).
set -euo pipefail
source "$(dirname "$0")/common.sh"
TAG="${1:-latest}"
if [ "$TAG" = latest ]; then URL="https://github.com/$RELEASE_REPO/releases/latest/download"
else URL="https://github.com/$RELEASE_REPO/releases/download/$TAG"; fi
command -v curl >/dev/null || die "curl is required"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
info "Downloading $RELEASE_ASSET from $RELEASE_REPO ($TAG)"
curl -fL --progress-bar -o "$TMP/$RELEASE_ASSET" "$URL/$RELEASE_ASSET" || die "download failed: is there a release with $RELEASE_ASSET?"
curl -fsSL -o "$TMP/$RELEASE_ASSET.sha256" "$URL/$RELEASE_ASSET.sha256" || die "checksum download failed"
(cd "$TMP" && sha256sum -c "$RELEASE_ASSET.sha256") || die "checksum mismatch"

info "Unpacking into $BUILD"
rm -rf "$BUILD/wine-root" "$BUILD/dxvk" "$BUILD/components"
mkdir -p "$BUILD"
tar -C "$BUILD" -xf "$TMP/$RELEASE_ASSET"
[ -x "$WINE_ROOT/bin/wine" ] || die "the release did not contain Wine"
info "Installed release $(cat "$BUILD/VERSION" 2>/dev/null || echo unknown)"

# Most apps need the VC++ runtime framework package (Microsoft.VCLibs.140.00). It is Microsoft's, so releases don't
# carry it: fetch the x64 package once into the framework store from Microsoft's winget release, refusing it unless it
# is exactly the known file. setup-prefix.sh fetches the other frameworks an app needs the same way.
if ! compgen -G "$FRAMEWORKS/Microsoft.VCLibs.140.00_*_x64/AppxManifest.xml" >/dev/null; then
    info "Downloading the VC++ runtime framework package (Microsoft.VCLibs.140.00 14.0.33519.0, x64)"
    python3 -I "$REPO/scripts/uwptool.py" download-framework Microsoft.VCLibs.140.00 "$FRAMEWORKS" ||
        echo "warning: couldn't download Microsoft.VCLibs.140.00; import it yourself with setup-prefix.sh --frameworks (see README)" >&2
fi
if compgen -G "$PREFIX/drive_c/$APPS_DIR_NAME/*/" >/dev/null; then
    info "Setting up the installed apps again for this build"
    "$REPO/scripts/setup-prefix.sh" --update-all || echo "warning: some apps were not updated; run scripts/setup-prefix.sh for them" >&2
else
    info "Next: scripts/setup-prefix.sh /path/to/package-folder"
fi
