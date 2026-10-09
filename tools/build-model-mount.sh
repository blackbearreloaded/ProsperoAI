#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$root/.deps/mihawk-vulkan-review/.deps/native/ps5-payload-sdk}
make -C "$root/payload/model_mount"
