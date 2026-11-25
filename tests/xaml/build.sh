#!/bin/bash
# Cross-compiles the XAML test apps with the Wine build's winegcc and WinRT headers (needs scripts/build-wine.sh first).
# Output: build/tests/xaml/xamlhello.exe (milestone 1), xamllayout.exe (milestone 2)
set -euo pipefail
source "$(dirname "$0")/../../scripts/common.sh"
[ -x "$BUILD/obj/wine/tools/winegcc/winegcc" ] || die "build Wine first (scripts/build-wine.sh)"
mkdir -p "$BUILD/tests/xaml"
for t in xamlhello xamllayout; do cp "$REPO/tests/xaml/$t.c" "$BUILD/tests/xaml/"; done
docker run --rm -u "$(id -u):$(id -g)" -v "$BUILD:/build" -w /build/obj/wine wine-uwp-build bash -c '
    set -e
    for t in xamlhello xamllayout; do
        x86_64-w64-mingw32-gcc -c -o /build/tests/xaml/$t.o /build/tests/xaml/$t.c -Iinclude -I/build/src/wine/include \
            -I/build/src/wine/include/msvcrt -D_MSVCR_VER=0 -D__WINE_PE_BUILD -D_UCRT -Wall -O1 -fno-strict-aliasing
        tools/winegcc/winegcc -o /build/tests/xaml/$t.exe --wine-objdir . -b x86_64-w64-mingw32 -mconsole \
            /build/tests/xaml/$t.o dlls/combase/x86_64-windows/libcombase.a dlls/user32/x86_64-windows/libuser32.a \
            dlls/gdi32/x86_64-windows/libgdi32.a libs/uuid/x86_64-windows/libuuid.a dlls/winecrt0/x86_64-windows/libwinecrt0.a \
            dlls/ucrtbase/x86_64-windows/libucrtbase.a dlls/kernel32/x86_64-windows/libkernel32.a dlls/ntdll/x86_64-windows/libntdll.a
    done'
info "built $BUILD/tests/xaml/xamlhello.exe and xamllayout.exe"
