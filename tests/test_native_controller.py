"""Compile the real UI controller against deterministic host runtime/storage fakes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import struct
import json

ROOT = Path(__file__).resolve().parents[1]


def tool(name, *args):
    """Runs a build tool that prints a folder (the kit staged at its pin, the baked fonts)."""
    return Path(subprocess.run(["bash", str(ROOT / "tools" / name), *args], check=True,
                               stdout=subprocess.PIPE, text=True).stdout.strip())


class NativeController(unittest.TestCase):
    def test_real_catalog_paging(self):
        with tempfile.TemporaryDirectory(prefix="prospero-catalog-") as directory:
            directory = Path(directory)
            models = directory / "models"
            models.mkdir()
            for index in range(24):
                model = models / f"variant-{index:02d}"
                model.mkdir()
                (model / "model.ps5lm").write_bytes(struct.pack(
                    "<4s11I", b"P5LM", 1, 256, 128, 1, 32, 2048, 4096, 14336, 32, 8, 32768))
                (model / "tokenizer.ps5tok").write_bytes(b"fixture")
                (model / "model.json").write_text(json.dumps({"name": f"Local variation {index}"}))
            binary = directory / "catalog-test"
            subprocess.run([os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O1",
                            "-fsanitize=address,undefined", "-ffunction-sections", "-fdata-sections",
                            "-Wl,--gc-sections", "-Wl,--wrap=fopen", "-I" + str(ROOT / "include"),
                            "-I" + str(ROOT / "src"), str(ROOT / "tests/native_catalog_test.cpp"),
                            str(ROOT / "src/gpt_runtime.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(models)], check=True, timeout=15)

    def test_allocator_realloc(self):
        with tempfile.TemporaryDirectory(prefix="prospero-allocator-") as directory:
            binary = Path(directory) / "allocator-test"
            subprocess.run([os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O1", "-pthread",
                            "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/native_allocator_test.cpp"),
                            str(ROOT / "tooling/native/app_cpp_runtime.cpp"),
                            str(ROOT / "src/agc_lifecycle.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_worker_state_and_unicode(self):
        with tempfile.TemporaryDirectory(prefix="prospero-controller-") as directory:
            directory = Path(directory)
            kit = tool("prepare-ui-kit.sh") / "src"
            fonts = tool("bake-fonts.sh")
            binary = directory / "controller-test"
            subprocess.run([os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-pthread",
                            "-g", "-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-Wall", "-Wextra", "-Werror", "-DPROSPERO_HOST",
                            f'-DPROSPERO_SETTINGS_PATH="{directory / "settings.cfg"}"',
                            "-I" + str(ROOT / "include"),
                            "-I" + str(kit),
                            str(ROOT / "tests/native_controller_test.cpp"),
                            str(ROOT / "src/native_app.cpp"),
                            str(ROOT / "src/media_preview.cpp"),
                            str(kit / "gfx/font.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(fonts / "inter-regular.huifont"),
                            str(fonts / "noto-sans-east-asian.huifont")], check=True, timeout=30)
