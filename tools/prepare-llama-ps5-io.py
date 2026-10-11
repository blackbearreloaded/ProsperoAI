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
source_root = parser.parse_args().source
source = source_root / 'src'
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

# PS5 titles do not expose libc's stderr in klog. Route each timestamp result
# through ggml's registered log callback so profiling remains observable.
vulkan = source_root / 'ggml/src/ggml-vulkan/ggml-vulkan.cpp'
text = vulkan.read_text()
marker = 'GGML_LOG_INFO("Vulkan Timings:\\n")'
if marker not in text:
    begin = text.index('    void print_timings(bool force = false) {')
    end = text.index('\n    std::string get_node_fusion_name', begin)
    body = '''    void print_timings(bool force = false) {
        if (timings.empty()) return;
        print_count++;
        if ((print_count % vk_perf_logger_frequency) != 0 && !force) return;
        print_count = 0;
        uint64_t total_all_op_times = 0;
        GGML_LOG_INFO("Vulkan Timings:\\n");
        for (const auto & t : timings) {
            uint64_t total_op_times = 0;
            for (const auto & time : t.second) total_op_times += time;
            std::ostringstream line;
            line << t.first << ": " << t.second.size() << " x "
                 << (total_op_times / t.second.size() / 1000.0) << " us = "
                 << (total_op_times / 1000.0) << " us";
            auto it = flops.find(t.first);
            if (it != flops.end() && it->second.size() == t.second.size()) {
                uint64_t total_op_flops = 0;
                for (const auto & elem : it->second) total_op_flops += elem;
                line << " (" << (double(total_op_flops) / 1.0e9) /
                    (double(total_op_times) / 1.0e9) << " GFLOPS/s)";
            }
            total_all_op_times += total_op_times;
            GGML_LOG_INFO("%s\\n", line.str().c_str());
        }
        GGML_LOG_INFO("Total time: %.3f us.\\n", total_all_op_times / 1000.0);
        timings.clear();
        flops.clear();
    }
'''
    vulkan.write_text(text[:begin] + body + text[end:])

# Oberon's RDNA2 UMA benefits from computing four adjacent standard-quant
# output rows per subgroup. A console A/B on Mistral 7B Q4_0 measured
# 65.51 versus 62.93 decode tokens/s (+4.1%). Keep this PS5-only rather than
# changing the generic upstream AMD heuristic.
replace(vulkan, '''        if (device->architecture == AMD_GCN) {
            rm_stdq = 2;
            rm_kq = 4;
            rm_stdq_int = 4;
        }
''', '''        if (device->architecture == AMD_GCN) {
            rm_stdq = 2;
            rm_kq = 4;
            rm_stdq_int = 4;
        } else if (device->architecture == AMD_RDNA2 && device->uma) {
            rm_stdq = 2;
        }
''')
