#!/usr/bin/env bash
# ps5-native-app-boilerplate - Native Linux/WSL application build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles, links, signs, validates, and assembles the root skeleton app.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
# The app folder and its ZIP are the only output: any other format name is refused.
output=${1:-Folder}
[[ ${output,,} == folder ]] || {
    echo "usage: tools/build.sh [Folder]" >&2
    exit 2
}
# What a build that is not a release calls itself, such as a pull request's number and
# commit. Checked before anything is built; written into the app folder further down.
if [[ -n ${BUILD_LABEL:-} ]]; then
    [[ $BUILD_LABEL =~ ^[A-Za-z0-9\ ,._#-]{1,40}$ ]] || {
        echo "BUILD_LABEL must be 1 to 40 letters, digits, spaces or , . _ # -" >&2
        exit 2
    }
fi

# There is one inference implementation. This entrypoint also serves build.ps1.
bash "$root/tools/build-vulkan-runtime.sh" ps5
exec bash "$root/tools/build-vulkan-release.sh"
