#!/bin/bash
# Packs a built tree (build-wine.sh, build-dxvk.sh and GE-Proton, or an unpacked release) into dist/ for a GitHub release:
#   dist/wine-uwp-x86_64.tar.xz         patched Wine, dxgi.dll, the four component DLLs, licences and source information
#   dist/wine-uwp-x86_64.tar.xz.sha256
# Usage: scripts/package-release.sh [version]     (default: RELEASE_VERSION from common.sh, e.g. 11.22-uwp3.0)
# Then: gh release create <version> dist/wine-uwp-x86_64.tar.xz dist/wine-uwp-x86_64.tar.xz.sha256 --notes-file dist/NOTES.md
set -euo pipefail
source "$(dirname "$0")/common.sh"
VERSION="${1:-$RELEASE_VERSION}"
built="$("$WINE_ROOT/bin/wine" --version 2>/dev/null || true)"
[ "$built" = "wine-$RELEASE_VERSION" ] || die "the Wine build says it is $built, not wine-$RELEASE_VERSION: rebuild with scripts/build-wine.sh after changing UWP_VERSION"
[ -x "$WINE_ROOT/bin/wine" ] || die "no Wine build in $WINE_ROOT: run scripts/build-wine.sh"
[ -f "$DXVK_DXGI" ] && [ -f "$DXVK_D3D11" ] && [ -f "$DXVK_D3D10CORE" ] || die "no dxgi.dll / d3d11.dll in $BUILD/dxvk: run scripts/build-dxvk.sh"
[ -z "$(git -C "$REPO" status --porcelain patches docker scripts 2>/dev/null)" ] || die "uncommitted changes in patches/, docker/ or scripts/: the release's source must match a commit"
[ -f "$BUILD/src/wine/COPYING.LIB" ] && [ -f "$BUILD/src/dxvk/LICENSE" ] || die "Wine and DXVK sources not found in $BUILD/src (their licence texts are packaged): build with build-wine.sh and build-dxvk.sh"
# Guard against shipping a private build: the public build never reads add-on licences from a file
if grep -qaP "uwp_emu|u\\x00w\\x00p\\x00_\\x00e\\x00m\\x00u" "$WINE_ROOT/lib/wine/x86_64-windows/twinapi.appcore.dll"; then die "this Wine build contains add-on (DLC) unlocking code and must not be released: rebuild with scripts/build-wine.sh"; fi
read -r VKD3D_DIR WINEDLL_DIR < <(find_components) || die "component DLLs not found: install $GE_PROTON_VERSION or set GE_PROTON"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/wine-root" "$STAGE/dxvk" "$STAGE/components" "$STAGE/licenses" "$STAGE/source"

info "Collecting Wine (without headers, man pages and static libs)"
cp -a "$BUILD/wine-root/usr" "$STAGE/wine-root/"
rm -rf "$STAGE/wine-root/usr/local/include" "$STAGE/wine-root/usr/local/share/man" "$STAGE/wine-root/usr/local/lib/"*.a
find "$STAGE/wine-root" -type f \( -name '*.so' -o -name '*.so.*' \) -exec strip --strip-unneeded {} + 2>/dev/null || true
cp "$DXVK_DXGI" "$STAGE/dxvk/dxgi.dll"
cp "$DXVK_D3D11" "$STAGE/dxvk/d3d11.dll"
cp "$DXVK_D3D10CORE" "$STAGE/dxvk/d3d10core.dll"
for dll in $COMPONENT_DLLS; do
    if [ -f "$VKD3D_DIR/$dll" ]; then cp "$VKD3D_DIR/$dll" "$STAGE/components/$dll"; else cp "$WINEDLL_DIR/$dll" "$STAGE/components/$dll"; fi
done

info "Adding licences and source information"
cp "$BUILD/src/wine/COPYING.LIB" "$STAGE/licenses/LGPL-2.1.txt"
cp "$BUILD/src/wine/LICENSE" "$STAGE/licenses/Wine-LICENSE.txt"
cp "$BUILD/src/dxvk/LICENSE" "$STAGE/licenses/DXVK-zlib.txt"
cp -r "$REPO/patches" "$REPO/docker" "$REPO/scripts" "$STAGE/source/"
cat > "$STAGE/SOURCE.txt" <<EOT
wine-uwp release $VERSION

Everything in this archive is free software. The source for each part:

* Wine ($WINE_TAG, LGPL 2.1 or later): the unmodified tag from $WINE_GIT plus patches/wine/ in source/ (apply in order with
  git apply). source/scripts/build-wine.sh and source/docker/ are the exact build recipe.
* dxvk/dxgi.dll, d3d11.dll and d3d10core.dll (DXVK, zlib licence): commit $DXVK_COMMIT of $DXVK_GIT plus patches/dxvk/ in source/.
  Build recipe: source/scripts/build-dxvk.sh.
* components/d3d12.dll, d3d12core.dll (vkd3d-proton, LGPL 2.1 or later) and wintypes.dll, windows.storage.dll (Wine, LGPL 2.1 or
  later) are unmodified copies from $GE_PROTON_VERSION by GloriousEggroll, https://github.com/GloriousEggroll/proton-ge-custom
  (tag $GE_PROTON_VERSION; its vkd3d-proton and wine sources are the submodules at that tag). Wine's own source is at
  https://gitlab.winehq.org/wine/wine and vkd3d-proton's at https://github.com/HansKristian-Work/vkd3d-proton.

If you cannot get any of the source from these places, ask in an issue at https://github.com/$RELEASE_REPO and it will be provided.
No Microsoft software and no game or app files are included.
EOT
echo "$VERSION" > "$STAGE/VERSION"

mkdir -p "$REPO/dist"
OUT="$REPO/dist/$RELEASE_ASSET"
info "Compressing (xz -T0)"
tar -C "$STAGE" -cf - . | xz -T0 -6 > "$OUT"
(cd "$REPO/dist" && sha256sum "$RELEASE_ASSET" > "$RELEASE_ASSET.sha256")
cat > "$REPO/dist/NOTES.md" <<EOT
Prebuilt Wine $WINE_TAG with UWP/WinRT support, a CoreWindow-capable DXVK \`dxgi.dll\`, and the D3D12/WinRT DLLs they need.

\`\`\`bash
git clone https://github.com/$RELEASE_REPO.git && cd $(basename "$RELEASE_REPO")
scripts/fetch-release.sh $VERSION
scripts/setup-prefix.sh /path/to/your/unpacked/uwp/app
scripts/run-app.sh
\`\`\`

Licences and source information are in \`SOURCE.txt\` and \`licenses/\` inside the archive. It contains no Microsoft software and no app files.
EOT
info "Wrote $OUT ($(du -h "$OUT" | cut -f1))"
cat "$REPO/dist/$RELEASE_ASSET.sha256"
