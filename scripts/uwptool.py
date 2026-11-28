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
  uwptool.py link-package PACKAGE_DIR APP_DIR  link the package's files into APP_DIR, with percent-encoded names (from a
                                           plain unzip of an .appx) under their real names
  uwptool.py download-framework NAME STORE  download and import one of FRAMEWORK_DOWNLOADS (Microsoft.VCLibs.140.00 ...)
  uwptool.py download-frameworks PACKAGE_DIR STORE
                                           download and import the frameworks the package needs that STORE lacks and
                                           Microsoft publishes (FRAMEWORK_DOWNLOADS); print DOWNLOADED/FAILED<TAB>name
  uwptool.py appinfo-json PACKAGE_DIR      print the app's manifest details (names, version, images, family) as JSON
  uwptool.py apps-json USER_DIR_NAME       turn list-apps.sh's lines on stdin into a JSON array (see list-apps.sh)
  uwptool.py family PACKAGE_DIR            print the package family name wine-uwp gives the app (its saves folder name)
  uwptool.py version PACKAGE_DIR           print the package version
  uwptool.py package-identity FILE         print the name and version of a package file (name<TAB>version)
  uwptool.py unregister REG_FILE           print a REGEDIT4 file removing what a registry file made by "registry"
                                           added for the app alone (its own WinRT classes, see remove-app.sh)
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
    # stand-ins for managed WinRT components that Windows hosts with its own CLRHost.dll (not in the package)
    "wpnapps.dll": ["NotificationsExtensions.TileContent.TileContentFactory", "GA.GoogleAnalytics"],
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


def blockmap_names(package_dir):
    """the package's file names as AppxBlockMap.xml records them (decoded), lower-case with "/", or None"""
    path = os.path.join(package_dir, "AppxBlockMap.xml")
    if not os.path.exists(path):
        return None
    root = ET.parse(path).getroot()
    return {f.get("Name").replace("\\", "/").lower() for ns in BLOCKMAP_NS for f in root.iter(ns + "File")}


def link_package(package_dir, app_dir):
    """Link the package's files into app_dir. Package archives store names percent-encoded ("edit%400.5x.png" for
    "edit@0.5x.png") and Windows decodes them on install; a package unpacked by a plain unzip keeps the encoded names,
    so apps can't find those files (Minecraft's menu icons). A name is linked decoded when AppxBlockMap.xml has the
    decoded name. Folders holding such names become real folders of links; everything else is linked as it is."""
    names = blockmap_names(package_dir)

    def decoded(rel):
        if names is None or "%" not in rel:
            return None
        name = urllib.parse.unquote(rel)
        return name if name != rel and name.lower() in names else None

    needs = set()   # relative folders holding (at any depth) a name to decode
    for top, dirs, files in os.walk(package_dir):
        for n in dirs + files:
            rel = os.path.relpath(os.path.join(top, n), package_dir).replace(os.sep, "/")
            if decoded(rel):
                parent = os.path.dirname(rel)
                while parent:
                    needs.add(parent)
                    parent = os.path.dirname(parent)

    def link_folder(src, dest, rel):
        for n in sorted(os.listdir(src)):
            child_rel = (rel + "/" + n) if rel else n
            target = os.path.join(dest, urllib.parse.unquote(n) if decoded(child_rel) else n)
            if child_rel in needs and os.path.isdir(os.path.join(src, n)):
                os.mkdir(target)
                link_folder(os.path.join(src, n), target, child_rel)
            else:
                os.symlink(os.path.join(src, n), target)

    os.makedirs(app_dir, exist_ok=True)
    # what an earlier run linked: links, and folders of links (never anything else)
    for n in os.listdir(app_dir):
        path = os.path.join(app_dir, n)
        if os.path.islink(path):
            os.remove(path)
        elif os.path.isdir(path) and os.path.exists(os.path.join(package_dir, n)) and \
                all(os.path.islink(os.path.join(t, f)) for t, _, fs in os.walk(path) for f in fs):
            shutil.rmtree(path)
    link_folder(package_dir, app_dir, "")
    renamed = sum(1 for top, _, files in os.walk(package_dir) for f in files
                  if decoded(os.path.relpath(os.path.join(top, f), package_dir).replace(os.sep, "/")))
    if renamed:
        print("decoded %d file names" % renamed)


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
            if dll.lower() == "clrhost.dll" and any(cls in c for c in WINE_CLASSES.values()):
                out += ["[-" + package_reg_key(package, cls)[1:], ""]
                continue
            # the manifest's Path (GearGame\Binaries\x.dll) is a Windows path relative to the package: escape it
            # for .reg. The global key an older setup wrote for the class is removed.
            out += ["[-" + reg_key(cls)[1:], "", package_reg_key(package, cls),
                    '"DllPath"="%s"' % dll.replace("\\", "\\\\"), ""]
    # marshaling of the app's own interfaces between apartments: a windows.activatableClass.proxyStub extension names
    # a DLL whose DllGetClassObject makes the proxy and stub (Asphalt 7's Microsoft.Xbox.dll, for ICallbackInvoker)
    for proxy in re.findall(r"<ProxyStub\b(.*?)</ProxyStub>", manifest, re.S):
        clsid = re.search(r'ClassId="([^"]+)"', proxy)
        path = re.search(r"<Path>([^<]+)</Path>", proxy)
        if not clsid or not path:
            continue
        clsid = "{" + clsid.group(1).strip("{}").upper() + "}"
        out += ["[HKEY_LOCAL_MACHINE\\Software\\Classes\\CLSID\\%s\\InprocServer32]" % clsid,
                '@="%s"' % (app_dir + "\\" + path.group(1)).replace("\\", "\\\\"),
                '"ThreadingModel"="Both"', ""]
        for iid in re.findall(r'InterfaceId="([^"]+)"', proxy):
            iid = "{" + iid.strip("{}").upper() + "}"
            out += ["[HKEY_LOCAL_MACHINE\\Software\\Classes\\Interface\\%s\\ProxyStubClsid32]" % iid,
                    '@="%s"' % clsid, ""]
    for dll, classes in WINE_CLASSES.items():
        for cls in classes:
            out += [reg_key(cls), '"DllPath"="C:\\\\windows\\\\system32\\\\%s"' % dll, ""]
    # write crash dumps to C:\crash.txt so bug reports have a backtrace
    out += ["[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug]",
            '"Debugger"="C:\\\\windows\\\\system32\\\\cmd.exe /c winedbg --auto %ld %ld > C:\\\\crash.txt 2>&1"',
            '"Auto"="1"', ""]
    sys.stdout.write("\r\n".join(out) + "\r\n")


def unregister(reg_file):
    """Undo the app's own part of a registry file written by registry(): its package's WinRT classes. The global keys
    it writes (Wine's stand-in classes, the crash handler) serve every app and stay; so do proxy/stub classes, which
    other apps built with the same library can share and which do nothing once the app's files are gone."""
    text = open(reg_file, encoding="utf-8", errors="replace").read()
    packages = sorted(set(re.findall(r"^\[HKEY_LOCAL_MACHINE\\Software\\Wine\\WindowsRuntime\\Packages\\([^\\\]]+)\\",
                                     text, re.M)))
    out = ["REGEDIT4", ""]
    for package in packages:
        out += ["[-HKEY_LOCAL_MACHINE\\Software\\Wine\\WindowsRuntime\\Packages\\%s]" % package, ""]
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


def best_image(package_dir, logo):
    """The largest variant (by file size) of a tile or splash image, any scale, PNG or JPEG, skipping high-contrast
    ones: for showing the app's artwork large, where best_icon's 256-pixel limit doesn't apply."""
    logo = logo.replace("\\", "/")
    folder = find_case_insensitive(package_dir, os.path.dirname(logo))
    base, ext = os.path.splitext(os.path.basename(logo))
    if folder is None:
        return None
    best = None
    try:
        names = os.listdir(folder)
    except OSError:
        return None
    for name in names:
        stem, e = os.path.splitext(name)
        if e.lower() != ext.lower() or not stem.lower().startswith(base.lower()):
            continue
        qualifiers = stem[len(base):].lower()
        if (qualifiers and not qualifiers.startswith(".")) or "contrast-" in qualifiers:
            continue
        path = os.path.join(folder, name)
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        if best is None or size > best[0]:
            best = (size, path)
    return os.path.abspath(best[1]) if best else None


# ---- resources.pri: names a manifest gives as "ms-resource:..." (port of patches 0100 and 0122's reader) ----

def _u16(d, o):
    return int.from_bytes(d[o:o + 2], "little")


def _u32(d, o):
    return int.from_bytes(d[o:o + 4], "little")


def _user_languages():
    """The user's languages, best first, from LANGUAGE and LANG ("en_NZ.UTF-8" -> "en-NZ"), then en-US."""
    out = []
    for var in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
        for v in os.environ.get(var, "").split(":"):
            v = v.split(".")[0].split("@")[0].replace("_", "-")
            if v and v not in ("C", "POSIX") and v not in out:
                out.append(v)
    if "en-US" not in out:
        out.append("en-US")
    return out[:8]


def _language_match(value, language):
    a, b = value.lower().split("-"), language.lower().split("-")
    n = 0
    for x, y in zip(a, b):
        if x != y:
            return n
        n += 1
    return n + 8 if len(a) == len(b) == n else n


def _language_score(value, languages):
    best = 0
    for one in value.split(";"):
        for i, lang in enumerate(languages):
            m = _language_match(one, lang)
            if m:
                best = max(best, (8 - i) * 16 + m)
    return best


class Pri:
    """The strings of a package's resources.pri, by name ("Resources/AppName"), in the user's language."""

    def __init__(self, path):
        d = open(path, "rb").read()
        if len(d) < 32 or d[:7] != b"mrm_pri":
            raise ValueError("not a PRI file")
        self.d = d
        toc, start, count = _u32(d, 16), _u32(d, 20), _u16(d, 24)
        self.sections = []
        for i in range(count):
            e = toc + i * 32
            ident = d[e:e + 16]
            offset, length = start + _u32(d, e + 24), _u32(d, e + 28)
            # a section: a 32-byte header, its content, an 8-byte footer
            self.sections.append((ident, d[offset + 32:offset + length - 8] if length >= 40 else b""))
        self.languages = _user_languages()

        def find(*names):
            for i, (ident, body) in enumerate(self.sections):
                if body and any(ident.startswith(n) for n in names):
                    return i
            return -1
        schema = find(b"[mrm_hschemaex]", b"[mrm_hschema]")
        self.map = find(b"[mrm_res_map2_]", b"[mrm_res_map__]")
        self.decisions = find(b"[mrm_decn_info]")
        if schema < 0 or self.map < 0:
            raise ValueError("no schema or resource map")
        self.names = self._schema(self.sections[schema][1], self.sections[schema][0].startswith(b"[mrm_hschemaex]"))

    def _schema(self, s, extended):
        if _u16(s, 0) != 1:
            raise ValueError("unknown schema version")
        unique_length, name_length = _u16(s, 2), _u16(s, 4)
        pos = 8
        extended_names = False
        if extended:
            extended_names = s[pos:pos + 13] == b"[def_hnamesx]"
            pos += 16
        scopes, items = _u32(s, pos + 12), _u32(s, pos + 16)
        pos += 20 + (unique_length + name_length) * 2 + 2
        unicode_length = _u32(s, pos + 16)
        pos += 24 + (4 if extended_names else 0)
        count = scopes + items
        entries = pos
        unicode = entries + count * 12 + scopes * 8 + items * 2
        ascii_ = unicode + unicode_length * 2
        table = []
        for i in range(count):
            p = entries + i * 12
            flags = s[p + 7]
            table.append((_u16(s, p), _u16(s, p + 2), flags, _u16(s, p + 8) | ((flags & 0xF) << 16), _u16(s, p + 10)))

        def name(e):
            parent, full_len, flags, off, _ = e
            if not full_len:
                return ""
            if flags & 0x20:
                end = s.index(b"\0", ascii_ + off)
                return s[ascii_ + off:end].decode("ascii", "replace")
            o = unicode + off * 2
            chars = []
            while o + 1 < len(s) and _u16(s, o):
                chars.append(_u16(s, o))
                o += 2
            return "".join(map(chr, chars))
        names = {}
        for i, e in enumerate(table):
            if e[2] & 0x10:
                continue   # a scope
            chain, k = [], i
            while k and len(chain) < 64:
                chain.append(k)
                if table[k][0] >= count or table[k][0] == k:
                    break
                k = table[k][0]
            path = "/".join(name(table[c]) for c in reversed(chain))
            names[path.lower()] = e[4]
        return names

    def _choose(self, decision, count):
        if self.decisions < 0 or count <= 1:
            return 0
        s = self.sections[self.decisions][1]
        distinct, qualifiers, sets, decisions, index_entries = (_u16(s, 0), _u16(s, 2), _u16(s, 4), _u16(s, 6), _u16(s, 8))
        data_length = _u16(s, 10) * 2
        quals = 12
        set_infos = quals + decisions * 4
        qual_infos = set_infos + sets * 4
        distinct_infos = qual_infos + qualifiers * 8
        index_table = distinct_infos + distinct * 12
        strings = index_table + index_entries * 2
        if decision >= decisions:
            return 0
        first_set, set_count = _u16(s, quals + decision * 4), _u16(s, quals + decision * 4 + 2)
        best, best_score = 0, 0
        for i in range(min(count, set_count)):
            if first_set + i >= index_entries:
                break
            st = _u16(s, index_table + (first_set + i) * 2)
            if st >= sets:
                break
            first_qual, qual_count = _u16(s, set_infos + st * 4), _u16(s, set_infos + st * 4 + 2)
            score = 1
            for j in range(qual_count):
                q = _u16(s, index_table + (first_qual + j) * 2)
                if q >= qualifiers:
                    break
                dq, fallback = _u16(s, qual_infos + q * 8), _u16(s, qual_infos + q * 8 + 4)
                if dq >= distinct:
                    break
                qtype = _u16(s, distinct_infos + dq * 12 + 2)
                offset = _u32(s, distinct_infos + dq * 12 + 8) * 2
                if qtype != 0 or offset >= data_length:
                    continue
                raw = s[strings + offset:strings + data_length]
                end = next((k for k in range(0, len(raw) - 1, 2) if raw[k] == 0 and raw[k + 1] == 0), len(raw))
                value = raw[:end].decode("utf-16-le", "replace")
                m = _language_score(value, self.languages)
                score = 1024 + m if m else (2 if fallback else 0)
            if score > best_score:
                best, best_score = i, score
        return best

    def string(self, name):
        item = self.names.get(name.lower())
        if item is None:
            return None
        s = self.sections[self.map][1]
        env_length, schema_ref_length, types = _u16(s, 0), _u16(s, 6), _u16(s, 10)
        map_count, group_count = _u16(s, 12), _u16(s, 14)
        info_count, candidates, data_length, large_length = _u32(s, 16), _u32(s, 20), _u32(s, 24), _u32(s, 28)
        types_pos = 32 + env_length + schema_ref_length
        maps = types_pos + types * 8
        groups = maps + map_count * 4
        infos = groups + group_count * 4
        pos = infos + info_count * 4
        large = pos
        large_maps = large_groups = large_infos = 0
        if large_length:
            large_maps, large_groups, large_infos = _u32(s, large), _u32(s, large + 4), _u32(s, large + 8)
        pos += large_length
        string_data = pos + candidates * 8
        total_maps, total_groups = map_count + large_maps, group_count + large_groups
        for i in range(total_maps):
            if i < map_count:
                first_item, group = _u16(s, maps + i * 4), _u16(s, maps + i * 4 + 2)
            else:
                first_item, group = _u32(s, large + 12 + (i - map_count) * 8), _u32(s, large + 12 + (i - map_count) * 8 + 4)
            if group < group_count:
                group_size, first_info = _u16(s, groups + group * 4), _u16(s, groups + group * 4 + 2)
            elif group < total_groups:
                g = large + 12 + large_maps * 8 + (group - group_count) * 8
                group_size, first_info = _u32(s, g), _u32(s, g + 4)
            else:
                group_size, first_info = 1, group - total_groups
            if not first_item <= item < first_item + group_size:
                continue
            j = first_info + (item - first_item)
            if j < info_count:
                decision, info = _u16(s, infos + j * 4), _u16(s, infos + j * 4 + 2)
            elif j - info_count < large_infos:
                e = large + 12 + large_maps * 8 + large_groups * 8 + (j - info_count) * 8
                decision, info = _u32(s, e), _u32(s, e + 4)
            else:
                return None
            info += self._choose(decision, candidates - info)
            if info >= candidates:
                return None
            c = pos + info * 8
            vtype = _u32(s, types_pos + s[c + 1] * 8 + 4) if s[c + 1] < types else 2
            if s[c] == 1:
                source, location, section = _u16(s, c + 2), _u16(s, c + 4), _u16(s, c + 6)
                if source or section >= len(self.sections):
                    return None
                data = self._data_item(self.sections[section][1], location)
            elif s[c] == 0:
                size, offset = _u16(s, c + 2), _u32(s, c + 4)
                data = s[string_data + offset:string_data + offset + size]
            else:
                return None
            if data is None:
                return None
            if vtype in (0, 1):   # UTF-16 string or path
                text = data.decode("utf-16-le", "replace")
            elif vtype in (3, 4, 5, 6):
                text = data.decode("utf-8", "replace")
            else:
                return None
            return text.split("\0")[0]
        return None

    @staticmethod
    def _data_item(s, index):
        strings, blobs = _u16(s, 4), _u16(s, 6)
        start = 12 + strings * 4 + blobs * 8
        if index < strings:
            offset, size = _u16(s, 12 + index * 4), _u16(s, 12 + index * 4 + 2)
        elif index < strings + blobs:
            offset, size = _u32(s, 12 + strings * 4 + (index - strings) * 8), _u32(s, 12 + strings * 4 + (index - strings) * 8 + 4)
        else:
            return None
        return s[start + offset:start + offset + size]


def resource_string(package_dir, ref, _cache={}):
    """The text of an "ms-resource:" reference from the manifest, from the package's resources.pri, or None."""
    if not ref.startswith("ms-resource:"):
        return ref
    path = find_case_insensitive(package_dir, "resources.pri")
    if not path:
        return None
    if path not in _cache:
        try:
            _cache[path] = Pri(path)
        except (OSError, ValueError, IndexError):
            _cache[path] = None
    pri = _cache[path]
    if not pri:
        return None
    name = ref[len("ms-resource:"):]
    if name.startswith("//"):
        name = name[2:].split("/", 1)[1] if "/" in name[2:] else ""   # ms-resource://Package.Name/Resources/X
    # a name without a leading slash is in the Resources map ("ms-resource:LandingPageTitle\\Text"); with one, it is
    # from the root ("ms-resource:///Resources/AppName")
    absolute = name.startswith("/")
    name = name.replace("\\", "/").lstrip("/")
    candidates = [name] if absolute else ["Resources/" + name, name]
    for n in candidates:
        try:
            v = pri.string(n)
        except (IndexError, ValueError):
            v = None
        if v:
            return v
    return None


def manifest_display_name(manifest, visual, package_dir=None):
    for m in (re.search(r'\bDisplayName="([^"]+)"', visual), re.search(r"<DisplayName>([^<]+)</DisplayName>", manifest)):
        if not m:
            continue
        value = html.unescape(m.group(1))   # "Solitaire &amp; Casual Games"
        if value.startswith("ms-resource:"):
            value = resource_string(package_dir, value) if package_dir else None
        if value:
            return value
    return re.search(r"<Identity\b[^>]*\bName=\"([^\"]+)\"", manifest).group(1)


def package_family(manifest):
    """The package family name, made the way wine-uwp's kernelbase makes it (patch 0001): Microsoft's publisher id
    for Microsoft's publisher string, otherwise thirteen zeros. Saves live in AppData\\Local\\Packages\\<family>."""
    ident = dict(re.findall(r'(\w+)="([^"]*)"', re.search(r"<Identity\b([^>]*)>", manifest).group(1)))
    ms = "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US"
    return "%s_%s" % (ident["Name"], "8wekyb3d8bbwe" if html.unescape(ident.get("Publisher", "")) == ms else "0000000000000")


def app_details(package_dir):
    """Everything a front end shows about a package folder, as a dict."""
    manifest = open(os.path.join(package_dir, "AppxManifest.xml"), encoding="utf-8").read()
    visual = re.search(r"<(?:\w+:)?VisualElements\b[^>]*>", manifest)
    visual = visual.group(0) if visual else ""
    tile = re.search(r"<(?:\w+:)?DefaultTile\b[^>]*>", manifest)
    tile = tile.group(0) if tile else ""
    splash = re.search(r"<(?:\w+:)?SplashScreen\b[^>]*>", manifest)
    splash = splash.group(0) if splash else ""
    icon = None
    # Windows 10 names, then Windows 8's (SmallLogo, Logo)
    # a 30-pixel SmallLogo loses to a 150-pixel Logo; otherwise the first found wins
    for attr in ("Square44x44Logo", "Square150x150Logo", "SmallLogo", "Logo"):
        m = re.search(r'\b%s="([^"]+)"' % attr, visual)
        found = best_icon(package_dir, m.group(1)) if m else None
        if found and (not icon or png_width(icon) < 64 <= png_width(found)):
            icon = found
        if icon and png_width(icon) >= 64:
            break

    def attr(tag, name):
        m = re.search(r'\b%s="([^"]*)"' % name, tag)
        return html.unescape(m.group(1)) if m else None

    def image(tag, name):
        value = attr(tag, name)
        return best_image(package_dir, value) if value else None

    def element(name):
        m = re.search(r"<%s>([^<]*)</%s>" % (name, name), manifest)
        return resource_string(package_dir, html.unescape(m.group(1).strip())) if m else None

    name, version, arch, _ = manifest_identity(manifest)
    exe = re.search(r"<Application\b[^>]*\bExecutable=\"([^\"]+)\"", manifest)
    description = attr(visual, "Description")
    return {
        "name": name,
        "version": version,
        "architecture": arch,
        "display_name": manifest_display_name(manifest, visual, package_dir),
        "publisher": element("PublisherDisplayName"),
        "description": (resource_string(package_dir, description) if description else None) or element("Description"),
        "background_color": attr(visual, "BackgroundColor"),
        "executable": exe.group(1) if exe else None,
        "family": package_family(manifest),
        "icon": os.path.abspath(icon) if icon else None,
        "square_image": image(tile, "Square310x310Logo") or image(visual, "Square150x150Logo") or image(visual, "Logo"),
        "wide_image": image(tile, "Wide310x150Logo") or image(tile, "WideLogo"),
        "splash_image": image(splash, "Image"),
        "splash_color": attr(splash, "BackgroundColor"),
        "dependencies": [dict(re.findall(r'(\w+)="([^"]*)"', d)).get("Name") for d in
                         re.findall(r"<PackageDependency\b([^>]*)>", manifest)],
    }


def appinfo(package_dir):
    details = app_details(package_dir)
    print("%s\t%s" % (details["display_name"].replace("\t", " "), details["icon"] or ""))


def apps_json(users_dir_name):
    """Read the apps set up, one per line from list-apps.sh (name, prefix, package folder, version slot, app folder,
    launcher file, settings file), and print them as a JSON array with their manifest details, saves folder and versions."""
    import json
    versions_root = os.path.join(os.environ["UWP_VERSIONS"])
    out = []
    for line in sys.stdin.read().splitlines():
        if not line.strip():
            continue
        name, prefix, source, slot, app_dir, launcher, env_file, own = (line.split("\t") + [""] * 8)[:8]
        try:
            details = app_details(app_dir)
        except Exception as e:   # a broken link or manifest shouldn't hide every other app
            details = {"name": name, "display_name": name, "error": str(e)}
        data_dir = os.path.join(prefix, "drive_c", "users", users_dir_name, "AppData", "Local", "Packages",
                                details.get("family") or name)
        versions = []
        folder = os.path.join(versions_root, name)
        if os.path.isdir(folder):
            for s in sorted(os.listdir(folder)):
                src_file = os.path.join(folder, s, "source")
                if not os.path.isfile(src_file):
                    continue
                vsrc = open(src_file, encoding="utf-8").read().strip()
                try:
                    vver = manifest_identity(open(os.path.join(vsrc, "AppxManifest.xml"), encoding="utf-8",
                                                  errors="replace").read())[1]
                except (OSError, TypeError):
                    vver = None
                versions.append({"slot": s, "source": vsrc, "version": vver, "active": s == slot,
                                 "data_dir": os.path.join(folder, s, "data"),
                                 "available": vver is not None})
        details.update({
            "prefix": prefix,
            "own_prefix": own == "1",
            "source": source or None,
            "slot": slot or None,
            "app_dir": app_dir,
            "data_dir": data_dir,
            "launcher": launcher or None,
            "settings_file": env_file,
            "versions": versions,
        })
        out.append(details)
    json.dump(out, sys.stdout, indent=1)
    sys.stdout.write("\n")


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


def open_package_file(source):
    try:
        return zipfile.ZipFile(source)
    except (zipfile.BadZipFile, OSError):
        sys.exit("%s is not a readable package: encrypted Store downloads (.eappx, .emsix) can't be used, "
                 "only plain .appx/.msix files" % source)


def package_file_identity(source):
    """(name, version) of a package file without unpacking it; for a bundle, the bundle's own identity, which has
    the same name as its application package."""
    z = open_package_file(source)
    names = set(z.namelist())
    for member in ("AppxMetadata/AppxBundleManifest.xml", "AppxManifest.xml"):
        if member in names:
            ident = re.search(r"<Identity\b([^>]*)>", z.read(member).decode("utf-8", "replace"))
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', ident.group(1))) if ident else {}
            if attrs.get("Name"):
                return attrs["Name"], attrs.get("Version", "0")
    sys.exit("%s has no AppxManifest.xml: is it a UWP package?" % source)


def extract_package(source, root):
    """Unpack a package file into root/<package name>, replacing an older version there; returns the folder."""
    z = open_package_file(source)
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
    # Windows' file system doesn't tell upper from lower case: a package can have both data/SWF/a.swf and
    # data/Swf/b.swf (Asphalt 7 does) and mean one folder. Linux would make two, and the app asks for b.swf in SWF.
    # A folder keeps the case it was first written with.
    folders, paths = {}, {}
    for info in infos:
        parts = urllib.parse.unquote(info.filename).replace("\\", "/").split("/")
        for i in range(len(parts) - 1):
            key = tuple(p.lower() for p in parts[:i + 1])
            parts[i] = folders.setdefault(key, parts[i])
        paths[info.filename] = "/".join(parts)
    for info in infos:
        target = os.path.normpath(os.path.join(tmp, paths[info.filename]))
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
    elif len(sys.argv) == 3 and sys.argv[1] == "appinfo-json":
        import json
        json.dump(app_details(sys.argv[2]), sys.stdout, indent=1)
        sys.stdout.write("\n")
    elif len(sys.argv) == 3 and sys.argv[1] == "apps-json":
        apps_json(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "family":
        print(package_family(open(os.path.join(sys.argv[2], "AppxManifest.xml"), encoding="utf-8").read()))
    elif len(sys.argv) == 3 and sys.argv[1] == "version":
        print(manifest_identity(open(os.path.join(sys.argv[2], "AppxManifest.xml"), encoding="utf-8",
                                     errors="replace").read())[1])
    elif len(sys.argv) == 3 and sys.argv[1] == "package-identity":
        print("%s\t%s" % package_file_identity(sys.argv[2]))
    elif len(sys.argv) == 3 and sys.argv[1] == "unregister":
        unregister(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "native-copy":
        native_copy(sys.argv[2], sys.argv[3])
    elif len(sys.argv) in (4, 5) and sys.argv[1] == "registry":
        registry(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) == 5 else None)
    elif len(sys.argv) == 4 and sys.argv[1] == "import-frameworks":
        import_frameworks(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "link-package":
        link_package(sys.argv[2], sys.argv[3])
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
