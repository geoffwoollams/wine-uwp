#!/bin/bash
# Downloads the prebuilt Wine, dxgi.dll and WinRT/D3D12 DLLs from a GitHub release and unpacks them into build/,
# so you can skip build-wine.sh, build-dxvk.sh and installing GE-Proton.
# Usage: scripts/fetch-release.sh [tag]      (default: the latest release)
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
info "Installed release $(cat "$BUILD/VERSION" 2>/dev/null || echo unknown). Next: scripts/setup-prefix.sh /path/to/package-folder"
