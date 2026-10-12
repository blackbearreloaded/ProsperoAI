#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=${PS5_PAYLOAD_SDK:-$root/.deps/mihawk-vulkan-review/.deps/native/ps5-payload-sdk}
mkdir -p "$root/build/stream-probe"
PS5_PAYLOAD_SDK="$sdk" "$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror \
    -DPROSPERO_PS5_STREAM_PROBE "$root/tools/stream-read-probe.c" -pthread \
    -o "$root/build/stream-probe/stream-read-probe.elf"
