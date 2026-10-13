#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add the pinned Bonsai private quant formats to the upstream GGML source tree.

This is a build-time compatibility adapter. It keeps the upstream GGML ABI and
does not link or expose the separate Prism llama runtime.
"""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one patch anchor, got {count}: {old[:90]!r}")
    path.write_text(text.replace(old, new, 1))


def extract_function(source: Path, signature: str) -> str:
    text = source.read_text()
    start = text.find(signature)
    if start < 0 or text.find(signature, start + 1) >= 0:
        raise SystemExit(f"{source}: expected one function {signature!r}")
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit(f"{source}: no function body for {signature!r}")
    depth = 0
    for end in range(brace, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start : end + 1]
    raise SystemExit(f"{source}: unterminated function {signature!r}")


def add_before(path: Path, anchor: str, addition: str) -> None:
    text = path.read_text()
    if addition.strip() in text:
        return
    if text.count(anchor) != 1:
        raise SystemExit(f"{path}: expected one insertion point {anchor!r}")
    path.write_text(text.replace(anchor, addition + "\n\n" + anchor, 1))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ggml-source", type=Path, required=True)
    parser.add_argument("--prism-source", type=Path, required=True)
    args = parser.parse_args()
    ggml = args.ggml_source
    prism = args.prism_source / "ggml"

    # The custom IDs are outside upstream's current range, so every ordinary
    # upstream enum value and its GGUF encoding stays unchanged.
    replace_once(
        ggml / "include/ggml.h",
        "        GGML_TYPE_COUNT   = 43,",
        "        GGML_TYPE_PQ2_0  = 142, // PrismML Q2, group 128\n"
        "        GGML_TYPE_PTQ1_0 = 143, // PrismML ternary, group 128\n"
        "        GGML_TYPE_COUNT   = 144,",
    )
    # Block definitions are copied from the exact Prism source pin that defines
    # the file format; they are not aliases for upstream Q2_0/TQ1_0 blocks.
    prism_common = (prism / "src/ggml-common.h").read_text()
    p0 = prism_common.index("#define QK_PQ2_0 128")
    p1 = prism_common.index("#define QK4_0", p0)
    private_blocks = prism_common[p0:p1]
    common = ggml / "src/ggml-common.h"
    text = common.read_text()
    anchor = "#define QK4_0"
    if "#define QK_PQ2_0 128" not in text:
        if text.count(anchor) != 1:
            raise SystemExit(f"{common}: upstream Q4_0 block anchor changed")
        common.write_text(text.replace(anchor, private_blocks + "\n" + anchor, 1))

    # Preserve upstream's generic tensor API while adding the exact codec entry
    # points and layouts used by the model file.
    qh = ggml / "src/ggml-quants.h"
    add_before(qh, "// Dequantization", """
GGML_API void quantize_row_pq2_0_ref(const float * GGML_RESTRICT, block_pq2_0 * GGML_RESTRICT, int64_t);
GGML_API void quantize_row_ptq1_0_ref(const float * GGML_RESTRICT, block_ptq1_0 * GGML_RESTRICT, int64_t);
GGML_API void dequantize_row_pq2_0(const block_pq2_0 * GGML_RESTRICT, float * GGML_RESTRICT, int64_t);
GGML_API void dequantize_row_ptq1_0(const block_ptq1_0 * GGML_RESTRICT, float * GGML_RESTRICT, int64_t);
GGML_API size_t quantize_pq2_0(const float * GGML_RESTRICT, void * GGML_RESTRICT, int64_t, int64_t, const float *);
GGML_API size_t quantize_ptq1_0(const float * GGML_RESTRICT, void * GGML_RESTRICT, int64_t, int64_t, const float *);
""")
    qsrc = ggml / "src/ggml-quants.c"
    psrc = prism / "src/ggml-quants.c"
    codec_functions = [
        extract_function(psrc, "void quantize_row_pq2_0_ref("),
        extract_function(psrc, "void dequantize_row_pq2_0("),
        extract_function(psrc, "void quantize_row_ptq1_0_ref("),
        extract_function(psrc, "void dequantize_row_ptq1_0("),
        extract_function(psrc, "size_t quantize_pq2_0("),
        extract_function(psrc, "size_t quantize_ptq1_0("),
    ]
    if "static const size_t ptq1_0_stages[3]" not in qsrc.read_text():
        add_before(qsrc, "void quantize_row_q4_0_ref(",
                   "static const size_t ptq1_0_stages[3] = {32, 16, 8};")
    for function in codec_functions:
        signature = function[: function.index("{")].strip()
        if signature not in qsrc.read_text():
            add_before(qsrc, "void quantize_row_q4_0_ref(", function)

    # Expose the quantized types to GGML's central type table.
    core = ggml / "src/ggml.c"
    add_before(core, "    [GGML_TYPE_Q4_0] = {", """
    [GGML_TYPE_PQ2_0] = {
        .type_name = "pq2_0", .blck_size = QK_PQ2_0, .type_size = sizeof(block_pq2_0),
        .is_quantized = true, .to_float = (ggml_to_float_t) dequantize_row_pq2_0,
        .from_float_ref = (ggml_from_float_t) quantize_row_pq2_0_ref,
    },
    [GGML_TYPE_PTQ1_0] = {
        .type_name = "ptq1_0", .blck_size = QK_PTQ1_0, .type_size = sizeof(block_ptq1_0),
        .is_quantized = true, .to_float = (ggml_to_float_t) dequantize_row_ptq1_0,
        .from_float_ref = (ggml_from_float_t) quantize_row_ptq1_0_ref,
    },
""")
    replace_once(
        core,
        "        case GGML_TYPE_Q2_0:    result = quantize_q2_0   (src + start, (char *) dst + start_row * row_size, nrows, n_per_row, imatrix); break;",
        "        case GGML_TYPE_Q2_0:    result = quantize_q2_0   (src + start, (char *) dst + start_row * row_size, nrows, n_per_row, imatrix); break;\n"
        "        case GGML_TYPE_PQ2_0: result = quantize_pq2_0(src + start, (char *) dst + start_row * row_size, nrows, n_per_row, imatrix); break;\n"
        "        case GGML_TYPE_PTQ1_0: result = quantize_ptq1_0(src + start, (char *) dst + start_row * row_size, nrows, n_per_row, imatrix); break;",
    )

    # CPU fallback: the selected PS5 Bonsai model stays in system memory until
    # its Vulkan kernels have been ported. Dot products intentionally use the
    # exact group-128 layouts and Q8_0 activations.
    cpu = ggml / "src/ggml-cpu"
    cpu_quants = cpu / "quants.c"
    cpu_prism_quants = prism / "src/ggml-cpu/quants.c"
    cpu_functions = [
        extract_function(cpu_prism_quants, "void ggml_vec_dot_pq2_0_q8_0_generic("),
        extract_function(cpu_prism_quants, "void ggml_vec_dot_ptq1_0_q8_0_generic("),
    ]
    cpu_wrappers = """
void quantize_row_pq2_0(const float * GGML_RESTRICT x, void * GGML_RESTRICT y, int64_t k) {
    quantize_row_pq2_0_ref(x, y, k);
}
void quantize_row_ptq1_0(const float * GGML_RESTRICT x, void * GGML_RESTRICT y, int64_t k) {
    quantize_row_ptq1_0_ref(x, y, k);
}
"""
    if "void quantize_row_pq2_0(" not in cpu_quants.read_text():
        add_before(cpu_quants, "void ggml_vec_dot_q4_0_q8_0_generic(", cpu_wrappers)
    for function in cpu_functions:
        signature = function[: function.index("{")].strip()
        if signature not in cpu_quants.read_text():
            add_before(cpu_quants, "void ggml_vec_dot_q4_0_q8_0_generic(", function)
    add_before(cpu / "quants.h", "void quantize_row_q4_0(", """
void quantize_row_pq2_0(const float * GGML_RESTRICT, void * GGML_RESTRICT, int64_t);
void quantize_row_ptq1_0(const float * GGML_RESTRICT, void * GGML_RESTRICT, int64_t);
""")
    add_before(cpu / "quants.h", "void ggml_vec_dot_q4_0_q8_0(", """
void ggml_vec_dot_pq2_0_q8_0(int, float * GGML_RESTRICT, size_t, const void * GGML_RESTRICT, size_t, const void * GGML_RESTRICT, size_t, int);
void ggml_vec_dot_ptq1_0_q8_0(int, float * GGML_RESTRICT, size_t, const void * GGML_RESTRICT, size_t, const void * GGML_RESTRICT, size_t, int);
""")
    fallback = cpu / "arch-fallback.h"
    replace_once(
        fallback,
        "#elif defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)\n// quants.c\n#define ggml_vec_dot_q2_0_q8_0_generic ggml_vec_dot_q2_0_q8_0",
        "#elif defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)\n// quants.c\n"
        "#define ggml_vec_dot_pq2_0_q8_0_generic ggml_vec_dot_pq2_0_q8_0\n"
        "#define ggml_vec_dot_ptq1_0_q8_0_generic ggml_vec_dot_ptq1_0_q8_0\n"
        "#define ggml_vec_dot_q2_0_q8_0_generic ggml_vec_dot_q2_0_q8_0",
    )
    add_before(cpu / "ggml-cpu.c", "    [GGML_TYPE_Q4_0] = {", """
    [GGML_TYPE_PQ2_0] = {
        .from_float = quantize_row_pq2_0,
        .vec_dot = ggml_vec_dot_pq2_0_q8_0,
        .vec_dot_type = GGML_TYPE_Q8_0,
        .nrows = 1,
    },
    [GGML_TYPE_PTQ1_0] = {
        .from_float = quantize_row_ptq1_0,
        .vec_dot = ggml_vec_dot_ptq1_0_q8_0,
        .vec_dot_type = GGML_TYPE_Q8_0,
        .nrows = 1,
    },
""")
    ops = cpu / "ops.cpp"
    text = ops.read_text()
    if "case GGML_TYPE_PQ2_0:" not in text:
        old = "        case GGML_TYPE_Q2_0:\n"
        if text.count(old) < 1:
            raise SystemExit(f"{ops}: no Q2_0 operation cases")
        text = text.replace(old, old + "        case GGML_TYPE_PQ2_0:\n        case GGML_TYPE_PTQ1_0:\n")
        ops.write_text(text)
    print(f"{ggml}: added Bonsai private quant format metadata and codecs")


if __name__ == "__main__":
    main()
