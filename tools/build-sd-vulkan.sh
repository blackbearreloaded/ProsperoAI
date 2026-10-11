#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SD must be built together with its actual ggml producer, not an imported 64-name archive.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec bash "$root/tools/build-vulkan-runtime.sh" ps5
