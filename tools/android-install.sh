#!/usr/bin/env bash
# Builds the Android64 .geode and installs it on the phone over wireless adb,
# then restarts Geode's launcher so it loads.
#
#   tools/android-install.sh            build + install + restart GD
#   tools/android-install.sh --no-build install the existing build
#
# Needs a phone paired once with `adb pair ip:port code` (Developer options >
# Wireless debugging > Pair device with pairing code). After that the phone is
# found by mDNS, so its IP and the connect port (new on every wireless-debugging
# restart) don't matter.
set -euo pipefail
export MSYS_NO_PATHCONV=1 # Git Bash would rewrite /sdcard/... into a Windows path

ADB="${ADB:-adb}"
command -v "$ADB" >/dev/null || ADB="/d/Android/Sdk/platform-tools/adb.exe"
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))" # adb.exe needs a Windows path
GEODE="$ROOT/build-android64/kamol1dn.lazer-ui.geode"
MODS="/sdcard/Android/media/com.geode.launcher/game/geode/mods/"
PACKAGE="com.geode.launcher"

if [[ "${1:-}" != "--no-build" ]]; then
    cmake --build "$ROOT/build-android64"
fi

"$ADB" start-server >/dev/null

# The adb server connects to paired phones it sees over mDNS by itself; give it
# a few seconds, then connect by hand to whatever mDNS reports.
device=""
for _ in $(seq 1 10); do
    device=$("$ADB" devices | awk 'NR > 1 && $2 == "device" { print $1; exit }')
    [[ -n "$device" ]] && break
    addr=$("$ADB" mdns services | awk '/_adb-tls-connect/ { print $3; exit }')
    [[ -n "$addr" ]] && "$ADB" connect "$addr" >/dev/null || true
    sleep 1
done
if [[ -z "$device" ]]; then
    echo "No phone found. Is Wireless debugging on, and the phone on the same Wi-Fi?" >&2
    exit 1
fi
echo "Phone: $device"

"$ADB" -s "$device" push "$GEODE" "$MODS"
"$ADB" -s "$device" shell am force-stop "$PACKAGE"
"$ADB" -s "$device" shell monkey -p "$PACKAGE" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
echo "Installed and restarted Geometry Dash."
