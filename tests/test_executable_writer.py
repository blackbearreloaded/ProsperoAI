# ps5-native-app-boilerplate - Host regression tests for the executable writer.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "tooling" / "native"
PT_LOAD = 1
PT_GNU_RELRO = 0x6474E552
PT_SCE_PROCPARAM = 0x61000001

STUB_SOURCE = "int sceKernelUsleep(unsigned int microseconds) { (void)microseconds; return 0; }\n"
PLAIN_SOURCE = """
int sceKernelUsleep(unsigned int microseconds);
void _start(void);
void _start(void) { (void)sceKernelUsleep(1); }
"""
RELOCATED_SOURCE = """
int sceKernelUsleep(unsigned int microseconds);
void _start(void);
static int first(void) { return 1; }
static int second(void) { return 2; }
int (*const table[])(void) = {first, second, first, second};
void _start(void) { (void)sceKernelUsleep((unsigned int)table[1]()); }
"""


def find_tool(*names):
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def program_headers(data):
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    headers = []
    for index in range(phnum):
        values = struct.unpack_from("<IIQQQQQQ", data, phoff + index * phentsize)
        headers.append(dict(zip(("type", "flags", "offset", "vaddr", "paddr", "filesz", "memsz", "align"), values)))
    return headers


def section_names(data):
    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    raw = [struct.unpack_from("<IIQQQQ", data, shoff + index * shentsize) for index in range(shnum)]
    strings = raw[shstrndx][4]
    return [data[strings + row[0]:data.index(b"\0", strings + row[0])].decode("ascii") for row in raw]


class ExecutableWriterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.clang = find_tool("clang-18", "clang")
        cls.lld = find_tool("ld.lld-18", "ld.lld")
        cls.cxx = find_tool("clang++-18", "clang++", "c++")
        if not (cls.clang and cls.lld and cls.cxx):
            raise unittest.SkipTest("host clang, lld and a C++20 compiler are required")
        cls.work = Path(tempfile.mkdtemp(prefix="ps5-executable-writer-"))
        cls.tool = cls.work / "ps5-native-tool"
        sources = [NATIVE / name for name in ("native_app_builder.cpp", "self_container.cpp",
                                               "elf_object.cpp", "sce_module_writer.cpp")]
        command = [cls.cxx, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                   *map(str, sources), "-o", str(cls.tool)]
        zlib_root = ROOT / ".deps" / "native" / "zlib" / "root"
        archives = list(zlib_root.rglob("libz.a")) if zlib_root.exists() else []
        command[-2:-2] = (["-I", str(zlib_root / "usr" / "include"), str(archives[0])]
                          if archives else ["-lz"])
        subprocess.run(command, check=True, capture_output=True, text=True)
        cls.stub = cls._link("libkernel", STUB_SOURCE, shared=True, soname="libkernel.prx",
                             exports="{ global: sceKernelUsleep; local: *; };\n")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "work"):
            shutil.rmtree(cls.work, ignore_errors=True)

    @classmethod
    def _link(cls, name, source, shared=False, soname=None, exports=None, inputs=()):
        directory = cls.work / name
        directory.mkdir(exist_ok=True)
        (directory / "main.c").write_text(source, encoding="utf-8")
        subprocess.run(
            [cls.clang, "-target", "x86_64-sie-ps5", "-fvisibility-nodllstorageclass=default",
             "-std=c11", "-O2", "-fPIC" if shared else "-fPIE", "-fno-plt",
             "-fno-stack-protector", "-fasynchronous-unwind-tables", "-nostdlib",
             "-c", str(directory / "main.c"), "-o", str(directory / "main.o")],
            check=True, capture_output=True, text=True,
        )
        output = directory / (f"{name}.so" if shared else f"{name}.elf")
        command = [cls.lld, "-m", "elf_x86_64", "-z", "max-page-size=0x4000", "--hash-style=gnu",
                   "--eh-frame-hdr", "-T", str(NATIVE / "ps5-pie.ld"), "-o", str(output),
                   str(directory / "main.o"), "--as-needed", *map(str, inputs)]
        if shared:
            command[3:3] = ["--shared", "-Bsymbolic", "-soname", soname]
            (directory / "exports.map").write_text(exports, encoding="utf-8")
            command += ["--version-script", str(directory / "exports.map")]
        else:
            command[3:3] = ["-pie", "-e", "_start"]
        subprocess.run(command, check=True, capture_output=True, text=True)
        return output

    def _convert(self, pie):
        output = pie.with_suffix(".ps5.elf")
        result = subprocess.run(
            [str(self.tool), "link", "--in", str(pie), "--out", str(output), "--stub",
             str(self.stub), "--file-name", "eboot.elf"], capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return output.read_bytes()

    def _check_layout(self, converted, pie):
        headers = program_headers(converted)
        loads = [header for header in headers if header["type"] == PT_LOAD and header["flags"]]
        for header in loads:
            self.assertEqual(header["offset"] % header["align"], header["vaddr"] % header["align"])
        relro = next(header for header in headers if header["type"] == PT_GNU_RELRO)
        relro_load = next(header for header in loads if header["vaddr"] == relro["vaddr"])
        self.assertEqual(relro_load["offset"], relro["offset"])
        param = next(header for header in headers if header["type"] == PT_SCE_PROCPARAM)
        self.assertTrue(relro["vaddr"] <= param["vaddr"] < relro["vaddr"] + relro["memsz"])
        source = pie.read_bytes()
        shoff = struct.unpack_from("<Q", source, 0x28)[0]
        shentsize, shnum, _ = struct.unpack_from("<HHH", source, 0x3A)
        names = section_names(source)
        origins = []
        for index in range(shnum):
            _, _, flags, address, offset = struct.unpack_from("<IIQQQ", source, shoff + index * shentsize)
            if flags & 0x2 and address == relro["vaddr"] and names[index] in (
                    ".data.rel.ro", ".got", ".got.plt", ".init_array"):
                origins.append(offset)
        self.assertIn(relro_load["offset"], origins)

    def test_program_with_relocated_read_only_data_converts(self):
        pie = self._link("relocated", RELOCATED_SOURCE, inputs=[self.stub])
        self.assertIn(".data.rel.ro", section_names(pie.read_bytes()))
        self._check_layout(self._convert(pie), pie)

    def test_program_without_data_rel_ro_converts(self):
        pie = self._link("plain", PLAIN_SOURCE, inputs=[self.stub])
        self.assertNotIn(".data.rel.ro", section_names(pie.read_bytes()))
        self._check_layout(self._convert(pie), pie)


if __name__ == "__main__":
    unittest.main()
