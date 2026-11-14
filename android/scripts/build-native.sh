#!/usr/bin/env bash
set -euo pipefail
android_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ndk_dir="${1:-${ANDROID_NDK_HOME:-}}"
if [[ -z "$ndk_dir" ]]; then
    sdk_dir="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
    ndk_dir="$sdk_dir/ndk/27.3.13750724"
fi
case "$(uname -s)" in
    Linux) host_tag=linux-x86_64 ;;
    Darwin) host_tag=darwin-x86_64 ;;
    *) echo 'Build with an NDK installation on Linux or macOS.' >&2; exit 1 ;;
esac
compiler="$ndk_dir/toolchains/llvm/prebuilt/$host_tag/bin/aarch64-linux-android28-clang++"
if [[ ! -x "$compiler" ]]; then
    echo "NDK compiler missing: $compiler" >&2
    echo 'Install NDK 27.3.13750724 or set ANDROID_NDK_HOME / -Pasm101.ndkPath.' >&2
    exit 1
fi
native_src="$android_dir/app/src/main/cpp"
output="$android_dir/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$output"
"$compiler" -std=c++20 -O2 -fPIC -shared -static-libstdc++ -Wall -Wextra -Werror \
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 -Wl,-soname,libasm101.so \
    "$native_src/Assembler.cpp" "$native_src/NativeDebugger.cpp" "$native_src/Jni.cpp" \
    -o "$output/libasm101.so"
"$ndk_dir/toolchains/llvm/prebuilt/$host_tag/bin/llvm-strip" --strip-unneeded "$output/libasm101.so"
