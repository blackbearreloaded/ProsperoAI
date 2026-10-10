#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build pinned RADV inputs on Ubuntu 24.04 using packaged LLVM 19 host libraries.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 "$root/tools/deps.py" fetch
ref="$root/.deps/mihawk-vulkan-review"
export PS5_PAYLOAD_SDK_FORK="$root/.deps/mihawk-sdk-review"
export PS5_MESA_FORK="$root/.deps/mihawk-mesa-review"
bash "$ref/tools/setup-native-dependencies.sh"
host="$root/.deps/vulkan-host"
if [[ ! -x $host/venv/bin/meson ]]; then
 python3 -m venv "$host/venv"
 "$host/venv/bin/pip" install meson==1.7.2 mako==1.3.10 ply==3.11 packaging==24.2 PyYAML==6.0.2
fi
mkdir -p "$host/bin"
cat > "$host/bin/glslangValidator" <<'WRAPPER'
#!/usr/bin/env bash
if [[ ${1:-} == --version ]]; then
 /usr/bin/glslangValidator --version | sed -n '1p'
else
 exec /usr/bin/glslangValidator "$@"
fi
WRAPPER
chmod +x "$host/bin/glslangValidator"
export MESON="$host/venv/bin/meson" NINJA=/usr/bin/ninja
export PATH="$host/bin:$host/venv/bin:/usr/lib/llvm-19/bin:$PATH"
export CC=clang-19 CXX=clang++-19
export CMAKE_PREFIX_PATH="/usr/lib/llvm-19${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
bash "$ref/tools/build-radv.sh" release
