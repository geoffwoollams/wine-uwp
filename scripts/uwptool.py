#!/usr/bin/env python3
"""Helpers for the setup-prefix.sh and run-app.sh scripts.

  uwptool.py verify PACKAGE_DIR           check every file against AppxBlockMap.xml (the package's SHA-256 list)
  uwptool.py vclibs SOURCE DEST           copy the VC++ UWP runtime (*_app.dll) from a VCLibs .appx or folder
  uwptool.py registry PACKAGE_DIR APP_DIR print a REGEDIT4 file with the WinRT class registrations to import
  uwptool.py info PACKAGE_DIR              print the package name and the executable of its first application (name<TAB>exe)
  uwptool.py native-copy SRC DEST         copy a Wine PE DLL so Wine loads it as a native DLL (clears the builtin marker)
"""
import base64
import hashlib
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile

BLOCKMAP_NS = ("{http://schemas.microsoft.com/appx/2015/blockmap}", "{http://schemas.microsoft.com/appx/2010/blockmap}")

# Classes provided by the wintypes/windows.storage DLLs (from GE-Proton) copied into the prefix (overriding Wine's own). The classes the patched Wine
# implements are registered by Wine itself (wineboot), from twinapi.appcore.rgs and the dlls' classes.idl.
WINE_CLASSES = {
    "wintypes.dll": ["Windows.Storage.Streams.DataReader"],
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


def reg_key(cls):
    return "[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\%s]" % cls


def registry(game_dir, app_dir):
    out = ["REGEDIT4", ""]
    manifest = open(os.path.join(game_dir, "AppxManifest.xml"), encoding="utf-8").read()
    for server in re.findall(r"<InProcessServer>(.*?)</InProcessServer>", manifest, re.S):
        dll = re.search(r"<Path>([^<]+)</Path>", server).group(1)
        for cls in re.findall(r'ActivatableClassId="([^"]+)"', server):
            out += [reg_key(cls), '"DllPath"="%s\\\\%s"' % (app_dir.replace("\\", "\\\\"), dll), ""]
    for dll, classes in WINE_CLASSES.items():
        for cls in classes:
            out += [reg_key(cls), '"DllPath"="C:\\\\windows\\\\system32\\\\%s"' % dll, ""]
    # write crash dumps to C:\crash.txt so bug reports have a backtrace
    out += ["[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug]",
            '"Debugger"="C:\\\\windows\\\\system32\\\\cmd.exe /c winedbg --auto %ld %ld > C:\\\\crash.txt 2>&1"',
            '"Auto"="1"', ""]
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


def native_copy(src, dest):
    data = bytearray(open(src, "rb").read())
    marker = b"Wine builtin DLL"
    if data[0x40:0x40 + len(marker)] == marker:
        data[0x40:0x40 + len(marker)] = bytes(len(marker))
    with open(dest, "wb") as out:
        out.write(data)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "verify":
        verify(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "info":
        info(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "vclibs":
        vclibs(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "native-copy":
        native_copy(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "registry":
        registry(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
