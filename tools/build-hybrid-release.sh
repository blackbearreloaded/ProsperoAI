#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
title=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["titleId"])' "$root/sce_sys/param.json")
[[ $title =~ ^PPSA[0-9]{5}$ ]] || exit 2
PROSPERO_APP_TITLE="$title" bash "$root/tools/build-vulkan-native.sh"
python3 - "$root" "$title" <<'PY'
from pathlib import Path
import shutil, sys, zipfile
root=Path(sys.argv[1]);title=sys.argv[2];target=root/'dist'/title
if target.exists(): shutil.rmtree(target)
shutil.copytree(root/'build/prospero-vulkan-native'/title,target)
(target/'models').mkdir()
shutil.copy(root/'models/README.txt',target/'models/README.txt')
(target/'INSTALL.txt').write_text('ProsperoAI '+title+'\n\nUpload this folder to /data/homebrew/'+title+'.\nModels, settings, conversations and logs are kept in /data/prosperoai; the app asks Lapy for that access at launch (a running Lapy service, or its own lapy.elf through the console-local ELF loader).\nRegister the app folder with your homebrew loader, then launch ProsperoAI.\nModels can be downloaded in Models or copied to /data/homebrew/prosperoai/models.\nText uses GGUF files; media uses the complete curated prepared folders.\nThe HTTP interface is at http://<PS5-IP>:11434/.\n')
archive=root/'dist'/f'{title}.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for path in sorted(target.rglob('*')): z.write(path,path.relative_to(target.parent))
PY
python3 "$root/tools/zip-open-modes.py" "$root/dist/$title.zip"
echo "Hybrid release: $root/dist/$title.zip"
