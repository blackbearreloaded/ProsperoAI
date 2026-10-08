#!/usr/bin/env bash
# ProsperoAI - Fetch the Noto Sans faces Chinese, Japanese and Korean text is drawn with.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/fetch-cjk-fonts.sh        (prints the folder)
#
# Noto Sans CJK is Copyright 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font Name
# 'Source', and is under the SIL Open Font License 1.1. The region subsets come from the Noto
# project's repository at one commit and are checked against the hashes below; like the kit, they
# are a dependency fetched when something is built and are not kept in this repository.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
commit=f8d157532fbfaeda587e826d4cd5b21a49186f7c
out="$root/.deps/fonts/noto-cjk-$commit"
base="https://raw.githubusercontent.com/notofonts/noto-cjk/$commit/Sans"
mkdir -p "$out"
while read -r hash path; do
    [[ -n $hash ]] || continue
    file="$out/${path##*/}"
    if [[ ! -s $file ]] || ! printf '%s  %s\n' "$hash" "$file" | sha256sum -c --status; then
        curl -fsSL --retry 3 -o "$file.part" "$base/$path"
        printf '%s  %s\n' "$hash" "$file.part" | sha256sum -c --status || {
            echo "fetch-cjk-fonts: $path does not match its pinned hash" >&2
            exit 1
        }
        mv "$file.part" "$file"
    fi
done <<'FILES'
faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9 SubsetOTF/SC/NotoSansSC-Regular.otf
69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68 SubsetOTF/KR/NotoSansKR-Regular.otf
6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2 LICENSE
FILES
printf '%s\n' "$out"
