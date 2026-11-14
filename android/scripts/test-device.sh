#!/usr/bin/env bash
set -euo pipefail
android_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
adb_bin="${ADB:-adb}"
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 ADB_SERIAL" >&2
    exit 1
fi
device="$1"
abis="$("$adb_bin" -s "$device" shell getprop ro.product.cpu.abilist | tr -d '\r')"
sdk="$("$adb_bin" -s "$device" shell getprop ro.build.version.sdk | tr -d '\r')"
case ",$abis," in
    *,arm64-v8a,*) ;;
    *) echo "Incompatible Android userspace: $abis (requires arm64-v8a)." >&2; exit 1 ;;
esac
if (( sdk < 28 )); then
    echo "Android SDK $sdk is too old; requires SDK 28+." >&2
    exit 1
fi
"$adb_bin" -s "$device" install -r "$android_dir/app/build/outputs/apk/debug/app-debug.apk"
"$adb_bin" -s "$device" install -r "$android_dir/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk"
result="$("$adb_bin" -s "$device" shell am instrument -w \
    com.xsoulp.asm101.test/com.xsoulp.asm101.NativeDebuggerInstrumentation)"
printf '%s\n' "$result"
if [[ "$result" != *'PASS:'* || "$result" == *'FAIL:'* ]]; then
    exit 1
fi
