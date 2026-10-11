#!/usr/bin/env python3
"""Apply guarded, repeatable PS5-only loading hooks to the pinned dependency."""
from pathlib import Path
import argparse

root = Path(__file__).resolve().parents[1]


def replace(path, before, after):
    text = path.read_text()
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit(f"{path}: PS5 I/O hook no longer matches pinned llama.cpp")
    path.write_text(text.replace(before, after))


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=root / '.deps/llama.cpp')
source = parser.parse_args().source / 'src'
replace(source / "llama-mmap.cpp", '#include "llama-mmap.h"',
        '#include "llama-mmap.h"\n#ifdef PROSPERO_PS5_MODEL_IO\n#include "ps5-model-io.hpp"\n#endif')
replace(source / "llama-mmap.cpp", '            std::size_t ret = std::fread(ptr, to_read, 1, fp);',
        '''#ifdef PROSPERO_PS5_MODEL_IO
            if (to_read == len && prospero_ps5_read_parallel(fp, ptr, to_read)) return;
#endif
            std::size_t ret = std::fread(ptr, to_read, 1, fp);''')
loader = source / 'llama-model-loader.cpp'
direct_size = ('LLAMA_DIRECT_IO_BUFFER_SIZE' if 'LLAMA_DIRECT_IO_BUFFER_SIZE' in loader.read_text()
               else '64 * 1024 * 1024')
replace(loader,
        f'const size_t buffer_size = alignment != 1 ? {direct_size} + 2 * alignment : 1 * 1024 * 1024;',
        f'''const size_t buffer_size = alignment != 1 ? {direct_size} + 2 * alignment :
#ifdef PROSPERO_PS5_MODEL_IO
        (std::getenv("PROSPERO_MODEL_IO_SERIAL") ? 1 : 8) * 1024 * 1024; // 32 MiB ring in parallel mode
#else
        1 * 1024 * 1024;
#endif''')
