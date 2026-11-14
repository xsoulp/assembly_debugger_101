#!/usr/bin/env bash
set -euo pipefail
demo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$demo_dir/../.." && pwd)"
cmake -S "$demo_dir" -B "$project_dir/build/linkedin-demo" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build/linkedin-demo" --parallel 4
QT_QPA_PLATFORM=offscreen "$project_dir/build/linkedin-demo/record_demo" "$demo_dir/asm101-linkedin-15s.mp4"
