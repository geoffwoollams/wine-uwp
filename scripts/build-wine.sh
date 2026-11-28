#!/bin/bash
# Builds Wine 11.22 with the patches in patches/wine/ inside a Debian container and installs it to build/wine-root.
# Usage: scripts/build-wine.sh [-j jobs]
set -euo pipefail
source "$(dirname "$0")/common.sh"
JOBS="$(nproc)"
[ "${1:-}" = "-j" ] && JOBS="$2"
command -v docker >/dev/null || die "docker is required (or build Wine yourself from build/src/wine, see README)"

mkdir -p "$BUILD/src" "$BUILD/obj/wine"
if [ ! -d "$BUILD/src/wine/.git" ]; then
    info "Cloning $WINE_TAG"
    git clone --depth 1 --branch "$WINE_TAG" "$WINE_GIT" "$BUILD/src/wine"
fi
cd "$BUILD/src/wine"
if [ -z "$(git status --porcelain)" ]; then
    info "Applying Wine patches"
    for p in "$REPO"/patches/wine/*.patch; do git apply --index "$p" || die "patch failed: $p"; done
else
    info "Wine source already patched (git status not clean), not re-applying"
fi

# Wine's version (wine --version, winecfg) comes from git describe in the source tree: an annotated tag on the same
# commit, newer than Wine's own, makes it wine-11.22-uwp5
if [ "$(git describe HEAD 2>/dev/null)" != "wine-$RELEASE_VERSION" ]; then
    git -c user.name=wine-uwp -c user.email=wine-uwp@localhost tag -a -f "wine-$RELEASE_VERSION" -m "wine-uwp $RELEASE_VERSION" HEAD >/dev/null
fi

info "Building the build container"
docker build -q -t wine-uwp-build -f "$REPO/docker/Dockerfile.wine" "$REPO/docker" >/dev/null

info "Configuring and building Wine ($JOBS jobs; a first build takes a while)"
docker run --rm -u "$(id -u):$(id -g)" -v "$BUILD:/build" -w /build/obj/wine wine-uwp-build bash -c "
    set -e
    [ -f Makefile ] || /build/src/wine/configure --enable-win64 --without-oss --without-gstreamer > configure.log
    make -j$JOBS > make.log 2>&1 || { tail -30 make.log; exit 1; }
    make install DESTDIR=/build/wine-root > install.log 2>&1"
[ -x "$WINE_ROOT/bin/wine" ] || die "build finished but $WINE_ROOT/bin/wine is missing"
info "Wine installed in $WINE_ROOT"
