#!/usr/bin/env python3
"""Build and render the actual native frontend using host-only runtime fixtures."""
import os
from pathlib import Path
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/ui-preview"
BUILD.mkdir(parents=True, exist_ok=True)
OUTPUT = Path(sys.argv[1]) if len(sys.argv) > 1 else BUILD / "screenshots"
OUTPUT.mkdir(parents=True, exist_ok=True)
FONT = BUILD / "multilingual.huifont"
converter = ROOT / "tools/convert-ui-font.py"
source_font = ROOT / "assets/ui/fonts/lvgl-bitmap/multilingual/Radio-24.fnt"
if not FONT.exists() or FONT.stat().st_mtime < max(converter.stat().st_mtime, source_font.stat().st_mtime):
    subprocess.run([sys.executable, str(converter), str(source_font), str(FONT)], check=True)

KIT = ROOT / "vendor/homebrew-ui/src"
sources = [ROOT / "src/native_app.cpp", ROOT / "src/native_ui.cpp", ROOT / "host/ui_preview.cpp"]
sources.append(ROOT / "host/platform_host.cpp")
sources.append(ROOT / "src/media_preview.cpp")
sources += [path for path in KIT.rglob("*.cpp") if "platform" not in path.parts]
cxx = os.environ.get("HOST_CXX", "clang++")
flags = ["-std=c++20", "-O2", "-g", "-fno-exceptions", "-fno-rtti", "-DGL_GLEXT_PROTOTYPES",
         "-DPROSPERO_HOST", f'-DPROSPERO_SETTINGS_PATH="{BUILD / "settings.cfg"}"',
         "-I" + str(ROOT / "include"), "-I" + str(KIT)]
headers_time = max(p.stat().st_mtime for base in (KIT, ROOT / "include") for p in base.rglob("*.hpp"))

def compile_source(source):
    obj = BUILD / (str(source.relative_to(ROOT)).replace("/", "_") + ".o")
    if not obj.exists() or obj.stat().st_mtime < max(source.stat().st_mtime, headers_time):
        subprocess.run([cxx, *flags, "-c", str(source), "-o", str(obj)], check=True)
    return str(obj)

with ThreadPoolExecutor(max_workers=int(os.environ.get("BUILD_JOBS", "6"))) as pool:
    objects = list(pool.map(compile_source, sources))
binary = BUILD / "prospero-ui-preview"
subprocess.run([cxx, *objects, "-pthread", "-lEGL", "-lGL", "-Wl,--wrap=fopen", "-o", str(binary)], check=True)
command = [str(binary), str(ROOT / "assets"), str(FONT), str(OUTPUT)]
if "--reel" in sys.argv:
    encoder = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo",
        "-pixel_format", "rgb24", "-video_size", "1280x720", "-framerate", "20", "-i", "-",
        "-vf", "vflip", "-c:v", "libwebp_anim", "-lossless", "1", "-q:v", "60",
        "-compression_level", "3", "-loop", "0", str(OUTPUT / "prospero-native-motion.webp")], stdin=subprocess.PIPE)
    try:
        subprocess.run(command, stdout=encoder.stdin, env={**os.environ, "PROSPERO_REEL": "1"}, check=True)
    finally:
        encoder.stdin.close()
    if encoder.wait() != 0:
        raise SystemExit("animation encoding failed")
else:
    subprocess.run(command, check=True)
from PIL import Image
for ppm in OUTPUT.glob("*.ppm"):
    with Image.open(ppm) as image:
        image.save(ppm.with_suffix(".png"))
    ppm.unlink()
