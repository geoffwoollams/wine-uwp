#!/usr/bin/env python3
"""Helpers for the setup-prefix.sh and run-app.sh scripts.

  uwptool.py verify PACKAGE_DIR           check every file against AppxBlockMap.xml (the package's SHA-256 list)
  uwptool.py vclibs SOURCE DEST           copy the VC++ UWP runtime (*_app.dll) from a VCLibs .appx or folder
  uwptool.py registry PACKAGE_DIR APP_DIR print a REGEDIT4 file with the WinRT class registrations to import
  uwptool.py info PACKAGE_DIR              print the package name and the executable of its first application (name<TAB>exe)
  uwptool.py native-copy SRC DEST         copy a Wine PE DLL so Wine loads it as a native DLL (clears the builtin marker)
  uwptool.py resolve PACKAGE_DIR PATH      print PATH inside the package with its real case (Windows ignores case), or fail
  uwptool.py appinfo PACKAGE_DIR           print the app's display name and its best icon file (name<TAB>icon path, or name<TAB>)
  uwptool.py import-frameworks SOURCE STORE  copy framework packages (VCLibs, .NET Native, WinUI ...) into the shared store:
                                           SOURCE is an unpacked package folder, an .appx/.msix, or a folder of either
  uwptool.py link-frameworks PACKAGE_DIR STORE APP_DIR
                                           link the DLLs of the frameworks the package depends on into APP_DIR, and print
                                           the ones the store doesn't have (MISSING<TAB>name<TAB>min version)
  uwptool.py timezone-mui EXPORTED_REG    print a REGEDIT4 file deleting the time zones' MUI_* names (IL2CPP apps)
  uwptool.py download-framework NAME STORE  download and import one of FRAMEWORK_DOWNLOADS (Microsoft.VCLibs.140.00 ...)
  uwptool.py download-frameworks PACKAGE_DIR STORE
                                           download and import the frameworks the package needs that STORE lacks and
                                           Microsoft publishes (FRAMEWORK_DOWNLOADS); print DOWNLOADED/FAILED<TAB>name
  uwptool.py extract-package FILE ROOT    unpack an .appx/.msix (or the x64 app package of an .appxbundle/.msixbundle) into
                                           ROOT/<package name> unless that version is already there; print the folder
"""
import base64
import hashlib
import html
import io
import urllib.parse
import os
import re
import shutil
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

BLOCKMAP_NS = ("{http://schemas.microsoft.com/appx/2015/blockmap}", "{http://schemas.microsoft.com/appx/2010/blockmap}")

# Classes provided by DLLs from GE-Proton copied into the prefix: windows.storage overrides Wine's own, wintypes is copied as
# uwp-wintypes.dll for its DataReader alone. The classes the patched Wine implements are registered by Wine itself
# (wineboot), from twinapi.appcore.rgs and the dlls' classes.idl.
WINE_CLASSES = {
    "uwp-wintypes.dll": ["Windows.Storage.Streams.DataReader"],
    "windows.storage.dll": ["Windows.Storage.Streams.InMemoryRandomAccessStream"],
}


def verify(game_dir):
    path = os.path.join(game_dir, "AppxBlockMap.xml")
    if not os.path.exists(path):
        sys.exit("AppxBlockMap.xml not found in %s: is this the app's package folder?" % game_dir)
    root = ET.parse(path).getroot()
    ok, bad, missing = 0, [], []
    files = [f for ns in BLOCKMAP_NS for f in root.iter(ns + "File")]
    for i, f in enumerate(files):
        name = f.get("Name").replace("\\", "/")
        full = os.path.join(game_dir, name)
        if not os.path.exists(full):
            missing.append(name)
            continue
        if os.path.getsize(full) != int(f.get("Size")):
            bad.append(name)
            continue
        file_hash = next((f.find(ns + "FileHash") for ns in BLOCKMAP_NS if f.find(ns + "FileHash") is not None), None)
        if file_hash is not None:
            with open(full, "rb") as fh:
                digest = hashlib.sha256()
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    digest.update(chunk)
            if digest.digest() != base64.b64decode(file_hash.get("Hash")):
                bad.append(name)
                continue
        else:
            blocks = [b for ns in BLOCKMAP_NS for b in f.iter(ns + "Block")]
            with open(full, "rb") as fh:
                if any(hashlib.sha256(fh.read(65536)).digest() != base64.b64decode(b.get("Hash")) for b in blocks):
                    bad.append(name)
                    continue
        ok += 1
        if i % 5000 == 0:
            print("  checked %d/%d files" % (i, len(files)), flush=True)
    print("verified %d files, %d modified, %d missing" % (ok, len(bad), len(missing)))
    for name in (bad + missing)[:20]:
        print("  problem: %s" % name)
    sys.exit(1 if bad or missing else 0)


def vclibs(source, dest):
    os.makedirs(dest, exist_ok=True)
    copied = []
    if os.path.isdir(source):
        for name in os.listdir(source):
            if name.lower().endswith("_app.dll"):
                shutil.copyfile(os.path.join(source, name), os.path.join(dest, name))
                copied.append(name)
    else:
        with zipfile.ZipFile(source) as z:
            for info in z.infolist():
                name = os.path.basename(info.filename)
                if name.lower().endswith("_app.dll") and "/" not in info.filename.strip("/"):
                    with z.open(info) as src, open(os.path.join(dest, name), "wb") as out:
                        shutil.copyfileobj(src, out)
                    copied.append(name)
    if not any(n.lower() == "vccorlib140_app.dll" for n in copied):
        sys.exit("no vccorlib140_app.dll found in %s: this does not look like the x64 Microsoft.VCLibs.140.00 package" % source)
    print("copied %d VC++ runtime DLLs: %s" % (len(copied), " ".join(sorted(copied))))


# ---- framework packages ----

def manifest_identity(manifest):
    ident = re.search(r"<Identity\b([^>]*)>", manifest)
    if not ident:
        return None
    attrs = dict(re.findall(r'(\w+)="([^"]*)"', ident.group(1)))
    framework = re.search(r"<Framework>\s*true\s*</Framework>", manifest, re.I) is not None
    return attrs.get("Name"), attrs.get("Version", "0"), attrs.get("ProcessorArchitecture", "neutral").lower(), framework


def version_key(version):
    return tuple(int(p) if p.isdigit() else 0 for p in version.split("."))


def import_one(source, store):
    """Import one framework package (folder or .appx/.msix); returns its store folder name, or None."""
    if os.path.isdir(source):
        path = os.path.join(source, "AppxManifest.xml")
        if not os.path.exists(path):
            return None
        manifest = open(path, encoding="utf-8", errors="replace").read()
    else:
        try:
            with zipfile.ZipFile(source) as z:
                manifest = z.read("AppxManifest.xml").decode("utf-8", "replace")
        except (zipfile.BadZipFile, KeyError, OSError):
            return None
    ident = manifest_identity(manifest)
    if not ident or not ident[3] or ident[2] not in ("x64", "neutral"):
        return None   # not a framework, or the 32-bit one
    name, version, arch, _ = ident
    folder = "%s_%s_%s" % (name, version, arch)
    dest = os.path.join(store, folder)
    if os.path.exists(os.path.join(dest, "AppxManifest.xml")):
        return folder
    tmp = dest + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    if os.path.isdir(source):
        shutil.copytree(source, tmp, symlinks=False)
    else:
        with zipfile.ZipFile(source) as z:
            for info in z.infolist():
                target = os.path.normpath(os.path.join(tmp, info.filename.replace("%20", " ")))
                if not target.startswith(os.path.normpath(tmp) + os.sep) or info.is_dir():
                    continue   # nothing outside the folder
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with z.open(info) as src, open(target, "wb") as out:
                    shutil.copyfileobj(src, out)
    os.replace(tmp, dest)
    return folder


def import_frameworks(source, store):
    os.makedirs(store, exist_ok=True)
    candidates = [source]
    if os.path.isdir(source) and not os.path.exists(os.path.join(source, "AppxManifest.xml")):
        candidates = [os.path.join(source, n) for n in sorted(os.listdir(source))]
    imported = []
    for path in candidates:
        if os.path.isdir(path) or path.lower().endswith((".appx", ".msix")):
            folder = import_one(path, store)
            if folder:
                imported.append(folder)
        elif path.lower().endswith(".dll") and os.path.basename(path).lower() in SYSTEM_DLLS:
            # Windows system DLLs a framework needs but doesn't carry (mrt100.dll, .NET Native's runtime service)
            os.makedirs(os.path.join(store, "system"), exist_ok=True)
            shutil.copyfile(path, os.path.join(store, "system", os.path.basename(path).lower()))
            imported.append("system/" + os.path.basename(path).lower())
    for folder in imported:
        print("framework %s" % folder)
    if not imported and os.path.exists(source):
        print("no x64 framework packages found in %s" % source, file=sys.stderr)


# from C:\Windows\System32, needed by framework packages: .NET Native's mrt100_app.dll calls mrt100.dll at start
SYSTEM_DLLS = {"mrt100.dll": "Microsoft.NET.Native.Runtime."}


def matched_frameworks(package_dir, store):
    """[(name, min version, store folder or None)] for the package's dependencies"""
    manifest = open(os.path.join(package_dir, "AppxManifest.xml"), encoding="utf-8").read()
    have = os.listdir(store) if os.path.isdir(store) else []
    result = []
    for dep in re.findall(r"<PackageDependency\b([^>]*)>", manifest):
        attrs = dict(re.findall(r'(\w+)="([^"]*)"', dep))
        name, minimum = attrs.get("Name"), attrs.get("MinVersion", "0")
        best = None
        for folder in have:
            if folder.endswith(".tmp") or not folder.startswith(name + "_"):
                continue
            rest = folder[len(name) + 1:].rsplit("_", 1)
            if len(rest) != 2 or rest[0].count(".") != 3 or version_key(rest[0]) < version_key(minimum):
                continue
            if best is None or version_key(rest[0]) > version_key(best[1]):
                best = (folder, rest[0])
        result.append((name, minimum, best[0] if best else None))
    return result


# Framework packages with public downloads from Microsoft: name -> (version, URL, file inside it or None, SHA-256 of
# the .appx). The VC++ runtimes come from winget's dependency bundle on GitHub, the others from Microsoft's NuGet
# packages on nuget.org; only the .appx is read out of those (HTTP range requests), not the whole archive.
WINGET_DEPS = "https://github.com/microsoft/winget-cli/releases/download/v1.29.380/DesktopAppInstaller_Dependencies.zip"
NUGET = "https://api.nuget.org/v3-flatcontainer/"
FRAMEWORK_DOWNLOADS = {
    "Microsoft.VCLibs.140.00": (
        "14.0.33519.0", WINGET_DEPS, "x64/Microsoft.VCLibs.140.00_14.0.33519.0_x64.appx",
        "9c17b521f9d690a1f504da5108ed6eec5669eb3a8fd1331eef43e40d84e74283"),
    "Microsoft.VCLibs.140.00.UWPDesktop": (
        "14.0.33728.0", WINGET_DEPS, "x64/Microsoft.VCLibs.140.00.UWPDesktop_14.0.33728.0_x64.appx",
        "077a3d1a5d0622bd3004dca85f5e192d6e98ec79b83d4aa06766759ea6c09c3d"),
    "Microsoft.UI.Xaml.2.8": (
        "8.2501.31001.0", NUGET + "microsoft.ui.xaml/2.8.7/microsoft.ui.xaml.2.8.7.nupkg",
        "tools/AppX/x64/Release/Microsoft.UI.Xaml.2.8.appx",
        "1421dba0b7f1da87e6ad52e17c25d38bcd2159bd7f91c85034cfa837238abaaa"),
    "Microsoft.NET.Native.Framework.2.2": (
        "2.2.29512.0",
        NUGET + "runtime.win10-x64.microsoft.net.native.sharedlibrary/2.2.8-rel-29512-01/"
        "runtime.win10-x64.microsoft.net.native.sharedlibrary.2.2.8-rel-29512-01.nupkg",
        "tools/SharedLibrary/ret/Native/Microsoft.NET.Native.Framework.2.2.appx",
        "c195047f4af9c3ff3d92c40e55a901cf7cfa08a679b8ca2543bb104bab2f7ff1"),
    "Microsoft.NET.Native.Runtime.2.2": (
        "2.2.28604.0",
        NUGET + "runtime.win10-x64.microsoft.net.native.compiler/2.2.9-rel-29512-01/"
        "runtime.win10-x64.microsoft.net.native.compiler.2.2.9-rel-29512-01.nupkg",
        "tools/Runtime/x64/Microsoft.NET.Native.Runtime.2.2.appx",
        "27fae660d8c609e020703bb463b6601e62811dc75db19975bcb7d68d959b527d"),
}


def http_range(url, start, end):
    """bytes start..end (inclusive) of url, and the resource's total size"""
    import urllib.request
    req = urllib.request.Request(url, headers={"Range": "bytes=%d-%d" % (start, end), "User-Agent": "wine-uwp"})
    with urllib.request.urlopen(req, timeout=60) as f:
        if f.status != 206:
            raise OSError("the server doesn't support range requests")
        return f.read(), int(f.headers["Content-Range"].rsplit("/", 1)[1])


def fetch_zip_member(url, member, out):
    """copy one file out of a remote zip into out, reading only the zip's directory and that file"""
    import zlib
    _, total = http_range(url, 0, 0)
    tail, _ = http_range(url, max(0, total - 65557), total - 1)
    i = tail.rfind(b"PK\x05\x06")
    if i < 0:
        raise OSError("not a zip file")
    cd_size, cd_offset = struct.unpack("<II", tail[i + 12:i + 20])
    if cd_offset == 0xffffffff:   # zip64
        j = tail.rfind(b"PK\x06\x06")
        cd_size, cd_offset = struct.unpack("<QQ", tail[j + 40:j + 56])
    cd, _ = http_range(url, cd_offset, cd_offset + cd_size - 1)
    p = 0
    while cd[p:p + 4] == b"PK\x01\x02":
        method, = struct.unpack("<H", cd[p + 10:p + 12])
        crc, csize, usize = struct.unpack("<III", cd[p + 16:p + 28])
        n, e, c = struct.unpack("<HHH", cd[p + 28:p + 34])
        offset, = struct.unpack("<I", cd[p + 42:p + 46])
        if cd[p + 46:p + 46 + n].decode("utf-8", "replace") == member:
            if 0xffffffff in (csize, usize, offset):
                raise OSError("zip64 members aren't supported")
            head, _ = http_range(url, offset, offset + 29)
            ln, le = struct.unpack("<HH", head[26:30])
            start = offset + 30 + ln + le
            data, _ = http_range(url, start, start + csize - 1)
            if method == 8:
                data = zlib.decompress(data, -15)
            elif method != 0:
                raise OSError("unsupported compression %d" % method)
            if len(data) != usize or zlib.crc32(data) != crc:
                raise OSError("%s is damaged" % member)
            with open(out, "wb") as f:
                f.write(data)
            return
        p += 46 + n + e + c
    raise OSError("%s not found" % member)


def download_framework(name, store):
    """download one FRAMEWORK_DOWNLOADS package, check it and import it into the store; raises on failure"""
    import contextlib
    import tempfile
    import urllib.request
    version, url, member, sha256 = FRAMEWORK_DOWNLOADS[name]
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, name + ".appx")
        if member:
            fetch_zip_member(url, member, out)
        else:
            req = urllib.request.Request(url, headers={"User-Agent": "wine-uwp"})
            with urllib.request.urlopen(req, timeout=60) as f, open(out, "wb") as o:
                shutil.copyfileobj(f, o)
        with open(out, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        if digest != sha256:
            raise OSError("checksum mismatch (%s)" % digest)
        with contextlib.redirect_stdout(io.StringIO()):
            import_frameworks(out, store)
    return version


def download_frameworks(package_dir, store):
    for name, minimum, folder in matched_frameworks(package_dir, store):
        if folder or name not in FRAMEWORK_DOWNLOADS or version_key(FRAMEWORK_DOWNLOADS[name][0]) < version_key(minimum):
            continue
        try:
            print("DOWNLOADED\t%s\t%s" % (name, download_framework(name, store)), flush=True)
        except Exception as e:
            print("FAILED\t%s\t%s" % (name, e), flush=True)


def link_frameworks(package_dir, store, app_dir):
    linked = []
    for name, minimum, folder in matched_frameworks(package_dir, store):
        if not folder:
            print("MISSING\t%s\t%s" % (name, minimum))
            continue
        src = os.path.join(store, folder)
        for entry in os.listdir(src):
            if not entry.lower().endswith((".dll", ".winmd")):
                continue
            target = os.path.join(app_dir, entry)
            # the VC++ runtime from VCLibs replaces copies the app may carry; the app's other files win
            if os.path.lexists(target):
                if not (name.startswith("Microsoft.VCLibs.") and os.path.islink(target)):
                    continue
                os.unlink(target)
            os.symlink(os.path.join(src, entry), target)
        linked.append(folder)
        # the system DLLs this framework needs
        for dll, prefix in SYSTEM_DLLS.items():
            path = os.path.join(store, "system", dll)
            target = os.path.join(app_dir, dll)
            if name.startswith(prefix):
                if os.path.exists(path):
                    if not os.path.lexists(target):
                        os.symlink(path, target)
                else:
                    print("MISSING\t%s (C:\\Windows\\System32 on Windows, for %s)\t-" % (dll, name))
    for folder in linked:
        print("LINKED\t%s" % folder)


def reg_key(cls):
    return "[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\%s]" % cls


def package_reg_key(package, cls):
    # an app's own classes are registered for its package only (combase looks there first, with DllPath relative
    # to the package folder): apps that ship the same classes, such as every Unity game's UnityPlayer ones, would
    # otherwise load each other's DLLs
    return "[HKEY_LOCAL_MACHINE\\Software\\Wine\\WindowsRuntime\\Packages\\%s\\ActivatableClassId\\%s]" % (package, cls)


def registry(game_dir, app_dir, store=None):
    out = ["REGEDIT4", ""]
    manifest = open(os.path.join(game_dir, "AppxManifest.xml"), encoding="utf-8").read()
    package = re.search(r"<Identity\b[^>]*\bName=\"([^\"]+)\"", manifest).group(1)
    # the frameworks' own classes too: their DLLs are linked into the app's folder
    if store:
        for _, _, folder in matched_frameworks(game_dir, store):
            if folder:
                manifest += open(os.path.join(store, folder, "AppxManifest.xml"), encoding="utf-8", errors="replace").read()
    for server in re.findall(r"<InProcessServer>(.*?)</InProcessServer>", manifest, re.S):
        dll = re.search(r"<Path>([^<]+)</Path>", server).group(1)
        for cls in re.findall(r'ActivatableClassId="([^"]+)"', server):
            # the manifest's Path (GearGame\Binaries\x.dll) is a Windows path relative to the package: escape it
            # for .reg. The global key an older setup wrote for the class is removed.
            out += ["[-" + reg_key(cls)[1:], "", package_reg_key(package, cls),
                    '"DllPath"="%s"' % dll.replace("\\", "\\\\"), ""]
    for dll, classes in WINE_CLASSES.items():
        for cls in classes:
            out += [reg_key(cls), '"DllPath"="C:\\\\windows\\\\system32\\\\%s"' % dll, ""]
    # write crash dumps to C:\crash.txt so bug reports have a backtrace
    out += ["[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug]",
            '"Debugger"="C:\\\\windows\\\\system32\\\\cmd.exe /c winedbg --auto %ld %ld > C:\\\\crash.txt 2>&1"',
            '"Auto"="1"', ""]
    sys.stdout.write("\r\n".join(out) + "\r\n")


def timezone_mui(exported):
    # Wine gives each time zone MUI_Display/MUI_Std/MUI_Dlt values ("@tzres.dll,-n"). .NET resolves those through
    # Environment.SystemDirectory, which IL2CPP refuses in a UWP app ("Failed getting the path of a special folder"),
    # so DateTime.Now threw in Microsoft Sudoku's start-up. Without them .NET uses the plain Display/Std/Dlt names.
    data = open(exported, "rb").read()
    text = data.decode("utf-16") if data[:2] in (b"\xff\xfe", b"\xfe\xff") else data.decode("utf-8", "replace")
    out, key = ["REGEDIT4", ""], None
    for line in text.splitlines():
        if line.startswith("[") and line.endswith("]"):
            key = line
        else:
            m = re.match(r'"(MUI_(?:Display|Std|Dlt))"=', line)
            if m and key:
                out += [key, '"%s"=-' % m.group(1), ""]
    sys.stdout.write("\r\n".join(out) + "\r\n")


def info(package_dir):
    path = os.path.join(package_dir, "AppxManifest.xml")
    if not os.path.exists(path):
        sys.exit("AppxManifest.xml not found in %s: is this an unpacked UWP package folder?" % package_dir)
    manifest = open(path, encoding="utf-8").read()
    name = re.search(r"<Identity\b[^>]*\bName=\"([^\"]+)\"", manifest)
    exe = re.search(r"<Application\b[^>]*\bExecutable=\"([^\"]+)\"", manifest)
    if not name or not exe:
        sys.exit("no package name or application executable in %s" % path)
    print("%s\t%s" % (name.group(1), exe.group(1)))


def png_width(path):
    try:
        with open(path, "rb") as f:
            header = f.read(24)
    except OSError:
        return 0
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return 0
    return int.from_bytes(header[16:20], "big")


def find_case_insensitive(root, relative):
    """root/relative with each part matched regardless of case, as Windows does ("Assets" finds "assets")."""
    path = root
    for part in [p for p in relative.split("/") if p]:
        try:
            match = next((n for n in os.listdir(path) if n.lower() == part.lower()), None)
        except OSError:
            return None
        if match is None:
            return None
        path = os.path.join(path, match)
    return path


def best_icon(package_dir, logo):
    """The largest image (up to 256 pixels) among the logo's size and scale variants, preferring the "unplated" ones
    (no coloured tile behind them) and skipping high-contrast ones; the same choice the CoreWindow makes for its icon."""
    logo = logo.replace("\\", "/")
    folder = find_case_insensitive(package_dir, os.path.dirname(logo))
    base = os.path.basename(logo)
    if base.lower().endswith(".png"):
        base = base[:-4]
    best = None
    if folder is None:
        return None
    try:
        names = os.listdir(folder)
    except OSError:
        return None
    for name in names:
        if not name.lower().endswith(".png") or not name.lower().startswith(base.lower()):
            continue
        qualifiers = name[len(base):-4].lower()
        if qualifiers and not qualifiers.startswith("."):
            continue
        if "contrast-" in qualifiers:
            continue
        path = os.path.join(folder, name)
        size = png_width(path)
        if not size or size > 256:
            continue
        rank = 0 if "altform-lightunplated" in qualifiers else 2 if "altform-unplated" in qualifiers else 1
        if best is None or (rank, size) > best[:2]:
            best = (rank, size, path)
    return best[2] if best else None


def appinfo(package_dir):
    manifest = open(os.path.join(package_dir, "AppxManifest.xml"), encoding="utf-8").read()
    visual = re.search(r"<(?:\w+:)?VisualElements\b[^>]*>", manifest)
    visual = visual.group(0) if visual else ""
    name = None
    for m in (re.search(r'\bDisplayName="([^"]+)"', visual), re.search(r"<DisplayName>([^<]+)</DisplayName>", manifest)):
        if m and not m.group(1).startswith("ms-resource:"):
            name = m.group(1)
            break
    if not name:
        name = re.search(r"<Identity\b[^>]*\bName=\"([^\"]+)\"", manifest).group(1)
    icon = None
    for attr in ("Square44x44Logo", "Square150x150Logo"):
        m = re.search(r'\b%s="([^"]+)"' % attr, visual)
        if m and (icon := best_icon(package_dir, m.group(1))):
            break
    name = html.unescape(name)   # "Solitaire &amp; Casual Games"
    print("%s\t%s" % (name.replace("\t", " "), os.path.abspath(icon) if icon else ""))


def native_copy(src, dest):
    data = bytearray(open(src, "rb").read())
    marker = b"Wine builtin DLL"
    if data[0x40:0x40 + len(marker)] == marker:
        data[0x40:0x40 + len(marker)] = bytes(len(marker))
    with open(dest, "wb") as out:
        out.write(data)


# ---- packages given as files ----

class FileSlice(io.RawIOBase):
    """A read-only view of part of a file: a package stored uncompressed inside a bundle, read in place."""
    def __init__(self, path, start, size):
        self.f, self.start, self.size, self.pos = open(path, "rb"), start, size, 0
    def readable(self): return True
    def seekable(self): return True
    def tell(self): return self.pos
    def seek(self, offset, whence=0):
        self.pos = max(0, offset if whence == 0 else self.pos + offset if whence == 1 else self.size + offset)
        return self.pos
    def readinto(self, buf):
        n = max(0, min(len(buf), self.size - self.pos))
        self.f.seek(self.start + self.pos)
        data = self.f.read(n)
        buf[:len(data)] = data
        self.pos += len(data)
        return len(data)


def open_member(path, info, scratch):
    """A bundle's inner package as a seekable file: in place when stored, else copied out to a temporary file."""
    if info.compress_type == zipfile.ZIP_STORED:
        with open(path, "rb") as f:
            f.seek(info.header_offset)
            header = f.read(30)
            start = info.header_offset + 30 + int.from_bytes(header[26:28], "little") + int.from_bytes(header[28:30], "little")
        return FileSlice(path, start, info.file_size)
    os.makedirs(scratch, exist_ok=True)
    tmp = open(os.path.join(scratch, ".bundle-member.tmp"), "w+b")
    with zipfile.ZipFile(path) as z, z.open(info) as src:
        shutil.copyfileobj(src, tmp, 1 << 20)
    os.unlink(tmp.name)   # removed when closed
    tmp.seek(0)
    return tmp


def extract_package(source, root):
    """Unpack a package file into root/<package name>, replacing an older version there; returns the folder."""
    try:
        z = zipfile.ZipFile(source)
    except (zipfile.BadZipFile, OSError):
        sys.exit("%s is not a readable package: encrypted Store downloads (.eappx, .emsix) can't be used, "
                 "only plain .appx/.msix files" % source)
    names = set(z.namelist())
    if "AppxMetadata/AppxBundleManifest.xml" in names:
        # a bundle: its x64 (or neutral) application package; the resource packages (languages, scales) aren't needed
        bundle = z.read("AppxMetadata/AppxBundleManifest.xml").decode("utf-8", "replace")
        chosen = None
        for tag in re.findall(r"<(?:\w+:)?Package\b[^>]*>", bundle):
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', tag))
            if attrs.get("Type", "application").lower() != "application":
                continue
            arch = attrs.get("Architecture", "neutral").lower()
            if arch == "x64" or (arch == "neutral" and not chosen):
                chosen = attrs.get("FileName")
        if not chosen:
            sys.exit("%s has no x64 application package" % source)
        inner = urllib.parse.unquote(chosen)
        member = chosen if chosen in names else inner
        print("Using %s from the bundle" % inner, file=sys.stderr)
        z = zipfile.ZipFile(open_member(source, z.getinfo(member), root))
        names = set(z.namelist())
    if "AppxManifest.xml" not in names:
        sys.exit("%s has no AppxManifest.xml: is it a UWP package?" % source)
    ident = manifest_identity(z.read("AppxManifest.xml").decode("utf-8", "replace"))
    if not ident or not ident[0]:
        sys.exit("%s: no package identity in AppxManifest.xml" % source)
    name, version, arch, _ = ident
    if arch not in ("x64", "neutral"):
        sys.exit("%s is a %s package; only x64 ones run here" % (source, arch))
    dest = os.path.join(root, name)
    manifest = os.path.join(dest, "AppxManifest.xml")
    if os.path.exists(manifest):
        old = manifest_identity(open(manifest, encoding="utf-8", errors="replace").read())
        if old and old[1] == version:
            print("%s %s is already unpacked in %s" % (name, version, dest), file=sys.stderr)
            print(dest)
            return
    infos = [i for i in z.infolist() if not i.is_dir()]
    total = sum(i.file_size for i in infos)
    print("Unpacking %s %s (%d files, %.1f GB) into %s" % (name, version, len(infos), total / 1e9, dest), file=sys.stderr)
    tmp = dest + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    base = os.path.normpath(tmp) + os.sep
    for info in infos:
        target = os.path.normpath(os.path.join(tmp, urllib.parse.unquote(info.filename)))
        if not target.startswith(base):
            continue   # nothing outside the folder
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with z.open(info) as src, open(target, "wb") as out:
            shutil.copyfileobj(src, out, 1 << 20)
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(root, exist_ok=True)
    os.replace(tmp, dest)
    print(dest)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "verify":
        verify(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "timezone-mui":
        timezone_mui(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "extract-package":
        extract_package(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "info":
        info(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "vclibs":
        vclibs(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "resolve":
        path = find_case_insensitive(sys.argv[2], sys.argv[3].replace("\\", "/"))
        if not path or not os.path.isfile(path):
            sys.exit(1)
        print(path)
    elif len(sys.argv) == 3 and sys.argv[1] == "appinfo":
        appinfo(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "native-copy":
        native_copy(sys.argv[2], sys.argv[3])
    elif len(sys.argv) in (4, 5) and sys.argv[1] == "registry":
        registry(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) == 5 else None)
    elif len(sys.argv) == 4 and sys.argv[1] == "import-frameworks":
        import_frameworks(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "download-framework" and sys.argv[2] in FRAMEWORK_DOWNLOADS:
        try:
            download_framework(sys.argv[2], sys.argv[3])
        except Exception as e:
            sys.exit("%s: %s" % (sys.argv[2], e))
    elif len(sys.argv) == 4 and sys.argv[1] == "download-frameworks":
        download_frameworks(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 5 and sys.argv[1] == "link-frameworks":
        link_frameworks(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(__doc__)
