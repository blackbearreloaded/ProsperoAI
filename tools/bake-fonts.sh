#!/usr/bin/env bash
# ProsperoAI - Bake the fonts the interface draws with into build/fonts.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/bake-fonts.sh        (prints the folder)
#
# The four faces of the interface (Inter Regular and SemiBold, Montserrat
# Medium, DejaVu Sans Mono) come from the kit's third_party/fonts and are baked
# by the kit's own baker with its "european" alphabet, so accented Latin, Greek
# and Cyrillic are drawn in the same face as the rest of a sentence.
#
# A model answers in whatever language it was asked in, so three more faces
# stand behind those four and are only loaded when a text needs them:
#   noto-sans-east-asian   Chinese and Japanese, from Noto Sans SC
#   noto-sans-korean       Hangul, from Noto Sans KR
#   legacy-multilingual    what the bitmap font of the first interface has that
#                          none of the others do (Arabic, Hebrew, Thai,
#                          Devanagari, rarer ideographs), drawn unshaped; small,
#                          so it is loaded with the first four
# The result is the same for the same inputs, so it is made once and reused.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kit=$(bash "$root/tools/prepare-ui-kit.sh" --kit)
cxx=$(command -v "${HOST_CXX:-clang++}")
out="$root/build/fonts"
host="$root/build/host"
baker_source="$kit/tools/font-baker/bake_font.cpp"
list_source="$root/tools/font-baker/bake_list.cpp"
legacy="$root/assets/fonts/legacy-multilingual"
mkdir -p "$out" "$host"

cjk=$(bash "$root/tools/fetch-cjk-fonts.sh")
stamp=$(cat "$baker_source" "$list_source" "$root/tools/cjk-ranges.py" \
    "$root/tools/convert-ui-font.py" "${BASH_SOURCE[0]}" \
    "$kit"/third_party/fonts/{Inter-Regular,Inter-SemiBold,Montserrat-Medium,DejaVuSansMono}.ttf \
    "$cjk"/NotoSans{SC,KR}-Regular.otf "$legacy"/Radio-24.fnt "$legacy"/Radio-24-*.rta |
    sha256sum | cut -d' ' -f1)
if [[ -f $out/.stamp && $(<"$out/.stamp") == "$stamp" ]]; then
    printf '%s\n' "$out"
    exit 0
fi

printf '==> [fonts] Baking the interface fonts\n' >&2
rm -f -- "$out"/*.huifont "$out"/*.txt "$out/.stamp"
"$cxx" -std=c++20 -O2 -w "$baker_source" -o "$host/bake_font"
while read -r ttf name size range; do
    [[ -n $ttf ]] || continue
    "$host/bake_font" "$kit/third_party/fonts/$ttf" "$out/$name.huifont" "$size" "$range" 2048 \
        european >&2
done <<'FONTS'
Inter-Regular.ttf inter-regular 48 6
Inter-SemiBold.ttf inter-semibold 48 6
Montserrat-Medium.ttf montserrat-medium 52 7
DejaVuSansMono.ttf dejavu-sans-mono 44 6
FONTS

"$cxx" -std=c++20 -O2 -w -I"$kit/third_party" -I"$kit/src" "$list_source" -o "$host/bake_list"
python3 "$root/tools/cjk-ranges.py" "$host/cjk-ranges" >&2
"$host/bake_list" "$cjk/NotoSansSC-Regular.otf" "$out/noto-sans-east-asian.huifont" 32 4 4096 \
    "$host/cjk-ranges/east-asian.txt" >&2
"$host/bake_list" "$cjk/NotoSansKR-Regular.otf" "$out/noto-sans-korean.huifont" 32 4 2048 \
    "$host/cjk-ranges/korean.txt" >&2
python3 "$root/tools/convert-ui-font.py" "$legacy/Radio-24.fnt" \
    "$out/legacy-multilingual.huifont" --exclude "$host/cjk-ranges/east-asian.txt" \
    --exclude "$host/cjk-ranges/korean.txt" >&2

cp "$cjk/LICENSE" "$out/NotoSansCJK-LICENSE.txt"
cp "$kit/third_party/fonts/Inter-LICENSE.txt" "$kit/third_party/fonts/Montserrat-LICENSE.txt" \
    "$kit/third_party/fonts/DejaVu-LICENSE.txt" "$out/"
for licence in "$legacy"/licenses/*; do
    cp "$licence" "$out/legacy-multilingual-${licence##*/}"
done
printf '%s\n' "$stamp" > "$out/.stamp"
printf '%s\n' "$out"
