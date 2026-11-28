#!/bin/bash
# Lists the apps set up, with their display names, versions and version slots.
# Usage: scripts/list-apps.sh [--json]
# --json prints a JSON array for front ends such as UWP Launcher: for each app its package name, display name,
# publisher, version, description, colours and images (icon, wide and square tiles, splash screen), executable, package
# family, the frameworks it depends on, its prefix (and whether it is its own), the package folder it was set up from,
# its version slot and the slots kept (each with its package folder, version and data folder), its folder in the prefix,
# its saves folder, its launcher (or null) and its per-user settings file (see run-app.sh).
set -euo pipefail
source "$(dirname "$0")/common.sh"
JSON=0
case "${1:-}" in
    --json) JSON=1 ;;
    "") ;;
    *) die "usage: $0 [--json]" ;;
esac
DESKTOP_LAUNCHERS="$APPLICATIONS"
lines() {
    local name prefix src slot own launcher
    while read -r name; do
        prefix="$(app_prefix "$name")" src="" slot="" own=0
        [ -f "$SOURCES/$name.source" ] && { src="$(sed -n 1p "$SOURCES/$name.source")"; slot="$(app_slot "$name")"; }
        [ "$prefix" != "$PREFIX" ] && own=1
        launcher="$DESKTOP_LAUNCHERS/wine-uwp-$name.desktop"
        [ -f "$launcher" ] || launcher=""
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$prefix" "$src" "$slot" \
            "$prefix/drive_c/$APPS_DIR_NAME/$name" "$launcher" "$(app_settings_file "$name")" "$own"
    done < <(list_apps | sort -u)
}
if [ "$JSON" = 1 ]; then
    lines | UWP_VERSIONS="$VERSIONS" python3 -I "$REPO/scripts/uwptool.py" apps-json "$(prefix_user)"
else
    lines | UWP_VERSIONS="$VERSIONS" python3 -I "$REPO/scripts/uwptool.py" apps-json "$(prefix_user)" |
        python3 -I -c '
import json, sys
apps = json.load(sys.stdin)
if not apps:
    print("No apps set up yet: scripts/setup-prefix.sh /path/to/package-folder-or.appx")
for a in apps:
    line = "%s (%s) %s" % (a.get("display_name"), a["name"], a.get("version") or "?")
    if a.get("slot"):
        line += ", data slot %s" % a["slot"]
    others = [v["slot"] for v in a.get("versions", []) if not v["active"]]
    if others:
        line += "; other versions: %s" % ", ".join(others)
    print(line)'
fi
