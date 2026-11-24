#!/bin/bash
# Builds DXVK's dxgi.dll with the CoreWindow swapchain patch (patches/dxvk/) inside a Debian container.
# Only dxgi.dll is used: vkd3d-proton (from GE-Proton or a release) provides D3D12 and needs DXVK's DXGI.
set -euo pipefail
source "$(dirname "$0")/common.sh"
command -v docker >/dev/null || die "docker is required"

mkdir -p "$BUILD/src" "$BUILD/dxvk"
if [ ! -d "$BUILD/src/dxvk/.git" ]; then
    info "Cloning DXVK"
    git clone "$DXVK_GIT" "$BUILD/src/dxvk"
    git -C "$BUILD/src/dxvk" checkout -q "$DXVK_COMMIT"
    git -C "$BUILD/src/dxvk" submodule update -q --init --recursive
fi
cd "$BUILD/src/dxvk"
if [ -z "$(git status --porcelain --ignore-submodules)" ]; then
    info "Applying DXVK patch"
    for p in "$REPO"/patches/dxvk/*.patch; do git apply "$p" || die "patch failed: $p"; done
fi

info "Building the build container"
docker build -q -t wine-uwp-dxvk-build -f "$REPO/docker/Dockerfile.dxvk" "$REPO/docker" >/dev/null

info "Building dxgi.dll"
cp "$REPO/docker/cross-mingw64.txt" "$BUILD/obj-cross-mingw64.txt"
docker run --rm -u "$(id -u):$(id -g)" -v "$BUILD:/build" wine-uwp-dxvk-build bash -c "
    set -e
    [ -f /build/obj/dxvk/build.ninja ] || meson setup --cross-file /build/obj-cross-mingw64.txt --buildtype release \
        -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false -Denable_d3d11=false \
        /build/obj/dxvk /build/src/dxvk > /build/obj-dxvk-setup.log
    ninja -C /build/obj/dxvk src/dxgi/dxgi.dll > /build/obj-dxvk-ninja.log 2>&1 || { tail -30 /build/obj-dxvk-ninja.log; exit 1; }
    cp /build/obj/dxvk/src/dxgi/dxgi.dll /build/dxvk/dxgi.dll"
info "Built $DXVK_DXGI"
