#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if ! cmake -S "$project_dir" -B "$project_dir/build-deb" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DASM101_EXAMPLE_PATH=/usr/share/asm101/examples/example.asm; then
  cat >&2 <<'EOF'

Package configuration failed. On Ubuntu 24.04, install the build dependencies:
  sudo apt-get update
  sudo apt-get install build-essential cmake ninja-build qt6-base-dev nasm binutils gdb
Then run ./packaging/build-deb.sh again.

If Qt6 is already installed in a custom location, configure CMake with
-DCMAKE_PREFIX_PATH=<Qt6-prefix> or -DQt6_DIR=<directory-containing-Qt6Config.cmake>.
EOF
  exit 1
fi
cmake --build "$project_dir/build-deb" --parallel 2
mkdir -p "$project_dir/dist"
cpack --config "$project_dir/build-deb/CPackConfig.cmake" -G DEB -B "$project_dir/dist"
(
  cd "$project_dir/dist"
  sha256sum asm101_*.deb > SHA256SUMS
)
