#!/bin/bash
# Cross-compiles the XAML test apps with the Wine build's winegcc and WinRT headers (needs scripts/build-wine.sh first).
# Output: build/tests/xaml/xamlhello.exe (milestone 1), xamllayout.exe (milestone 2), xamlmarkup.exe + *.xbf (milestone 3)
set -euo pipefail
source "$(dirname "$0")/../../scripts/common.sh"
[ -x "$BUILD/obj/wine/tools/winegcc/winegcc" ] || die "build Wine first (scripts/build-wine.sh)"
mkdir -p "$BUILD/tests/xaml"
for t in xamlhello xamllayout xamlmarkup; do cp "$REPO/tests/xaml/$t.c" "$BUILD/tests/xaml/"; done
# the markup test's XAML, compiled to XBF the way Visual Studio would
for x in "$REPO"/tests/xaml/markup/*.xaml; do
    python3 -I "$REPO/scripts/xbf.py" compile "$x" "$BUILD/tests/xaml/$(basename "${x%.xaml}").xbf" >/dev/null
done
cp "$REPO"/tests/xaml/loose/*.xaml "$BUILD/tests/xaml/"   # loose markup, loaded as text
# a 40x20 test picture for Image and ImageBrush: the left half red, the right half blue (made here, not committed)
python3 -I - "$BUILD/tests/xaml/halves.png" <<'PY'
import struct, sys, zlib
w, h = 40, 20
rows = b"".join(b"\0" + b"".join(b"\xff\0\0\xff" if x < w // 2 else b"\0\0\xff\xff" for x in range(w)) for _ in range(h))
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
docker run --rm -u "$(id -u):$(id -g)" -v "$BUILD:/build" -w /build/obj/wine wine-uwp-build bash -c '
    set -e
    for t in xamlhello xamllayout xamlmarkup; do
        x86_64-w64-mingw32-gcc -c -o /build/tests/xaml/$t.o /build/tests/xaml/$t.c -Iinclude -I/build/src/wine/include \
            -I/build/src/wine/include/msvcrt -D_MSVCR_VER=0 -D__WINE_PE_BUILD -D_UCRT -Wall -O1 -fno-strict-aliasing
        tools/winegcc/winegcc -o /build/tests/xaml/$t.exe --wine-objdir . -b x86_64-w64-mingw32 -mconsole \
            /build/tests/xaml/$t.o dlls/combase/x86_64-windows/libcombase.a dlls/user32/x86_64-windows/libuser32.a \
            dlls/gdi32/x86_64-windows/libgdi32.a dlls/advapi32/x86_64-windows/libadvapi32.a libs/uuid/x86_64-windows/libuuid.a libs/winecrt0/x86_64-windows/libwinecrt0.a \
            dlls/ucrtbase/x86_64-windows/libucrtbase.a dlls/kernel32/x86_64-windows/libkernel32.a dlls/ntdll/x86_64-windows/libntdll.a
    done'
info "built xamlhello.exe, xamllayout.exe, xamlmarkup.exe and the markup in $BUILD/tests/xaml"
