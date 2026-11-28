#!/bin/bash
# Tests for setup-prefix.sh's version slots, list-apps.sh and remove-app.sh, in a throwaway data folder (its own prefix,
# so nothing of yours is touched). Needs the patched Wine in build/ (fetch-release.sh or build-wine.sh), like setup does.
# Usage: tests/scripts/run.sh        (about a minute: it creates a Wine prefix)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
T="$(mktemp -d)"
trap 'WINEPREFIX="$T/data/prefix" "$REPO/build/wine-root/usr/local/bin/wineserver" -k 2>/dev/null; rm -rf "$T" || true' EXIT
# found before HOME moves (a source build takes the component DLLs from GE-Proton)
export GE_PROTON="${GE_PROTON:-$(bash -c 'source "$1/scripts/common.sh"; find_ge_proton' _ "$REPO" || true)}"
export UWP_DATA_DIR="$T/data" XDG_DATA_HOME="$T/xdg" HOME="$T/home"
mkdir -p "$HOME"
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }

# two versions of a package, as folders, and a third as an .appx file; the "executable" only has to exist
make_pkg() {
    mkdir -p "$1/Assets"
    cat > "$1/AppxManifest.xml" <<EOT
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10" xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10">
  <Identity Name="WineUwp.Test" Publisher="CN=wine-uwp tests" Version="$2" ProcessorArchitecture="x64" />
  <Properties><DisplayName>Test App</DisplayName><PublisherDisplayName>wine-uwp</PublisherDisplayName></Properties>
  <Applications><Application Id="App" Executable="test.exe" EntryPoint="Test.App">
    <uap:VisualElements DisplayName="Test App" Square150x150Logo="Assets\\Logo.png" Square44x44Logo="Assets\\Small.png" Description="Test" BackgroundColor="#102030" />
  </Application></Applications>
</Package>
EOT
    : > "$1/test.exe"
}
make_pkg "$T/v1" 1.0.0.0
make_pkg "$T/v2" 2.0.0.0
make_pkg "$T/v3src" 3.0.0.0
(cd "$T/v3src" && python3 -I -c 'import sys, zipfile, os
with zipfile.ZipFile(sys.argv[1], "w") as z:
    for root, _, files in os.walk("."):
        for f in files: z.write(os.path.join(root, f), os.path.relpath(os.path.join(root, f), "."))' "$T/v3.appx")

S="$REPO/scripts"
DATA="$T/data"
U="$(id -un)"
quiet() { "$@" > "$T/out.txt" 2>&1 || { cat "$T/out.txt"; return 1; }; }

quiet "$S/setup-prefix.sh" "$T/v1" --no-shortcut --no-download
SAVES="$DATA/prefix/drive_c/users/$U/AppData/Local/Packages/WineUwp.Test_0000000000000"
mkdir -p "$SAVES/LocalState" && echo v1 > "$SAVES/LocalState/save.txt"
echo 'export TEST_SETTING=one' > "$DATA/apps/WineUwp.Test.env"
check "set up without a slot: saves are a folder" '[ -d "$SAVES" ] && [ ! -L "$SAVES" ]'
check "list-apps.sh --json lists it" '"$S/list-apps.sh" --json | python3 -c "import json,sys; a=json.load(sys.stdin); assert a[0][\"name\"]==\"WineUwp.Test\" and a[0][\"slot\"] is None and a[0][\"version\"]==\"1.0.0.0\""'

quiet "$S/setup-prefix.sh" "$T/v2" --no-shortcut --no-download --data-slot 2.0.0.0
check "first slot: old version kept as slot 1.0.0.0" '[ "$(cat "$DATA/versions/WineUwp.Test/1.0.0.0/source")" = "$T/v1" ]'
check "first slot: old saves moved with it" '[ "$(cat "$DATA/versions/WineUwp.Test/1.0.0.0/data/LocalState/save.txt")" = v1 ]'
check "first slot: old settings file moved with it" '[ -f "$DATA/apps/WineUwp.Test@1.0.0.0.env" ] && [ ! -f "$DATA/apps/WineUwp.Test.env" ]'
check "saves folder links to the new slot" '[ "$(readlink "$SAVES")" = "$DATA/versions/WineUwp.Test/2.0.0.0/data" ]'
check "slot recorded" '[ "$(sed -n 4p "$DATA/apps/WineUwp.Test.source")" = 2.0.0.0 ]'
echo v2 > "$SAVES/save2.txt"

quiet "$S/setup-prefix.sh" "$T/v3.appx" --no-shortcut --no-download --data-slot 3.0.0.0
check "package file with a slot unpacks into the slot" '[ -f "$DATA/versions/WineUwp.Test/3.0.0.0/WineUwp.Test/AppxManifest.xml" ] && [ ! -d "$DATA/packages/WineUwp.Test" ]'
check "list-apps.sh --json lists three versions" '"$S/list-apps.sh" --json | python3 -c "import json,sys; a=json.load(sys.stdin)[0]; assert [v[\"slot\"] for v in a[\"versions\"]]==[\"1.0.0.0\",\"2.0.0.0\",\"3.0.0.0\"] and a[\"slot\"]==\"3.0.0.0\", a"'

quiet "$S/setup-prefix.sh" --switch WineUwp.Test 1.0.0.0 --no-shortcut --no-download
check "--switch back to 1.0.0.0 sees its saves" '[ "$(cat "$SAVES/LocalState/save.txt")" = v1 ]'
check "--switch: settings file of the slot" '[ "$("$S/../scripts/list-apps.sh" --json | python3 -c "import json,sys; print(json.load(sys.stdin)[0][\"settings_file\"])")" = "$DATA/apps/WineUwp.Test@1.0.0.0.env" ]'
quiet "$S/setup-prefix.sh" --update-all
check "--update-all keeps the slot" '[ "$(sed -n 4p "$DATA/apps/WineUwp.Test.source")" = 1.0.0.0 ]'

check "remove-app.sh --slot refuses the version in use" '! "$S/remove-app.sh" WineUwp.Test --slot 1.0.0.0 --yes >/dev/null 2>&1'
quiet "$S/remove-app.sh" WineUwp.Test --slot 3.0.0.0 --yes
check "remove-app.sh --slot removes its unpacked copy, keeps its saves" '[ ! -e "$DATA/versions/WineUwp.Test/3.0.0.0/WineUwp.Test" ] && [ -d "$DATA/versions/WineUwp.Test/3.0.0.0/data" ]'

quiet "$S/remove-app.sh" WineUwp.Test --yes
check "remove-app.sh: app folder gone" '[ ! -e "$DATA/prefix/drive_c/UWP/WineUwp.Test" ]'
check "remove-app.sh: record gone" '[ ! -e "$DATA/apps/WineUwp.Test.source" ]'
check "remove-app.sh: package folders untouched" '[ -f "$T/v1/test.exe" ] && [ -f "$T/v2/AppxManifest.xml" ]'
check "remove-app.sh: saves kept without --data" '[ -f "$DATA/versions/WineUwp.Test/1.0.0.0/data/LocalState/save.txt" ] && [ -f "$DATA/versions/WineUwp.Test/2.0.0.0/data/save2.txt" ]'
check "remove-app.sh: no longer listed" '[ "$("$S/list-apps.sh" --json | python3 -c "import json,sys; print(len(json.load(sys.stdin)))")" = 0 ]'

quiet "$S/setup-prefix.sh" "$T/v1" --no-shortcut --no-download
check "set up again unslotted: the saves link is still there" '[ -L "$SAVES" ]'
quiet "$S/remove-app.sh" WineUwp.Test --data --yes
check "remove-app.sh --data: saves gone" '[ ! -e "$SAVES" ] && [ ! -L "$SAVES" ] && [ ! -e "$DATA/versions/WineUwp.Test" ]'
check "remove-app.sh --data: package folder still untouched" '[ -f "$T/v1/test.exe" ]'
check "remove-app.sh refuses odd names" '! "$S/remove-app.sh" "../x" --yes >/dev/null 2>&1'

[ "$fails" = 0 ] && echo "all passed" || { echo "$fails failed"; exit 1; }
