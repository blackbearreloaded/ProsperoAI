#!/usr/bin/env python3
"""Apply guarded source compatibility edits to a disposable pinned audio.cpp checkout."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

PIN = "886076fc89c09b9902592aa4bfbd12922d4a9e26"
ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str) -> None:
    source = path.read_text()
    if (new and new in source) or (not new and old not in source):
        return
    if source.count(old) != 1:
        raise SystemExit(f"{path}: expected one known patch site; inspect source changes")
    path.write_text(source.replace(old, new, 1))


def adapt_weak_canonical(path: Path) -> None:
    text = path.read_text()
    signature = "std::filesystem::path ps5_weakly_canonical(const std::filesystem::path & path) {"
    while signature in text:
        start = text.index(signature)
        opening = text.index("{", start)
        closing = text.index("\n}", opening) + 2
        text = text[:start] + text[closing:]
    text = text.replace("std::filesystem::weakly_canonical(", "ps5_weakly_canonical(")
    if "ps5_weakly_canonical(" not in text:
        return
    marker = "namespace engine::"
    at = text.find(marker)
    if at < 0:
        raise SystemExit(f"{path}: namespace insertion point for filesystem adapter not found")
    helper = '''namespace {
std::filesystem::path ps5_weakly_canonical(const std::filesystem::path & path) {
#if defined(__FreeBSD__)
    return path.lexically_normal();
#else
    return std::filesystem::weakly_canonical(path);
#endif
}
} // namespace

'''
    path.write_text(text[:at] + helper + text[at:])


def omit_cmake_sources(path: Path) -> None:
    excluded = {
        "src/framework/codecs/wan_video_vae_runtime.cpp",
        "src/framework/modules/vocoders/bigvgan_vocoder.cpp",
        "src/framework/modules/optimizations/fast_conv_modules.cpp",
        "src/framework/modules/optimizations/fast_projection_modules.cpp",
        "src/framework/modules/attention/common_relative_attention.cpp",
        "src/framework/modules/speaker_encoders/ecapa_tdnn_runtime.cpp",
        "src/framework/modules/speaker_encoders/titanet_runtime.cpp",
        "src/framework/modules/transformers/causal_decoder.cpp",
        "src/framework/modules/transformers/causal_decoder_runtime.cpp",
        "src/framework/modules/transformers/qwen35_decoder_runtime.cpp",
        "src/framework/modules/text_encoders/qwen3_vl_encoder_runtime.cpp",
        "src/framework/modules/speech_encoders/qwen3_audio_encoder_runtime.cpp",
        "src/framework/modules/transformers/gemma_decoder.cpp",
        "src/framework/tokenizers/hf_tokenizer_json.cpp",
        "src/framework/tokenizers/qwen_bpe_bundle.cpp",
        "$<TARGET_OBJECTS:engine_model_silero_vad>",
        "$<TARGET_OBJECTS:engine_model_marblenet_vad>",
    }
    lines = path.read_text().splitlines(keepends=True)
    for entry in excluded:
        matches = [i for i, line in enumerate(lines) if line.strip() == entry]
        if len(matches) > 1:
            raise SystemExit(f"{path}: duplicate CMake entry for {entry}")
        if matches:
            del lines[matches[0]]
    path.write_text("".join(lines))


def include_sa3_dependencies(path: Path) -> None:
    entries = (
        "src/framework/modules/text_encoders/t5_base_encoder.cpp",
        "src/framework/modules/text_encoders/t5_gemma_encoder.cpp",
    )
    source = path.read_text()
    present = [entry in source for entry in entries]
    if all(present):
        return
    if any(present):
        raise SystemExit(f"{path}: partial Stable Audio text-encoder source selection")
    anchor = "    src/framework/modules/positional_modules.cpp\n"
    if source.count(anchor) != 1:
        raise SystemExit(f"{path}: expected one source insertion point for Stable Audio encoders")
    insertion = anchor + "".join(f"    {entry}\n" for entry in entries)
    path.write_text(source.replace(anchor, insertion, 1))


def include_ace_step_dependencies(path: Path) -> None:
    """Restore shared runtime pieces omitted from the smaller Stable Audio build."""
    source = path.read_text()
    required = {
        "src/framework/modules/transformers/decoder.cpp":
            "    src/framework/modules/transformers/transformer_blocks.cpp\n",
        "src/framework/modules/optimizations/fast_kv_modules.cpp":
            "    src/framework/modules/structural_modules.cpp\n",
        "src/framework/tokenizers/llama_bpe.cpp":
            "    src/framework/tokenizers/sentencepiece.cpp\n",
        "external/llama_tokenizer/bpe-core.cpp":
            "    src/framework/tokenizers/llama_bpe.cpp\n",
        "external/llama_tokenizer/unicode.cpp":
            "    external/llama_tokenizer/bpe-core.cpp\n",
        "external/llama_tokenizer/unicode-data.cpp":
            "    external/llama_tokenizer/unicode.cpp\n",
    }
    for entry, anchor in required.items():
        line = f"    {entry}\n"
        if line in source:
            continue
        if source.count(anchor) != 1:
            raise SystemExit(f"{path}: cannot restore ACE-Step dependency {entry}")
        source = source.replace(anchor, anchor + line, 1)
    path.write_text(source)


def adapt_ace_step_ggml_round(source: Path) -> None:
    replace_once(
        source / "src/framework/modules/transformers/decoder.cpp",
        "return core::wrap_tensor(ggml_round_bf16(ctx.ggml, input.tensor), input.shape, GGML_TYPE_F32);",
        "return core::wrap_tensor(ggml_cast(ctx.ggml, ggml_cast(ctx.ggml, input.tensor, GGML_TYPE_BF16), GGML_TYPE_F32), input.shape, GGML_TYPE_F32);",
    )


def adapt_gguf_streaming(path: Path) -> None:
    """Keep PS5 model inspection metadata-only; never map/copy the whole checkpoint."""
    source = path.read_text()
    marker = "// ProsperoAI: bounded GGUF tensor reads"
    if marker in source:
        new_budget = "constexpr size_t max_tensor_bytes = 1024ull * 1024 * 1024;"
        if new_budget in source:
            return
        old_budgets = (
            "constexpr size_t max_tensor_bytes = 256ull * 1024 * 1024;",
            "constexpr size_t max_tensor_bytes = 512ull * 1024 * 1024;",
        )
        old_budget = next((budget for budget in old_budgets if source.count(budget) == 1), None)
        if old_budget is None:
            raise SystemExit(f"{path}: expected one known GGUF read budget")
        path.write_text(source.replace(old_budget, new_budget, 1))
        return
    begin = source.index("class GgufTensorSource final : public TensorSource {")
    end = source.index("\nstd::unordered_map<std::string, std::string> parse_indexed_tensor_weight_map", begin)
    section = source[begin:end]
    def patch(old: str, new: str) -> None:
        nonlocal section
        if section.count(old) != 1:
            raise SystemExit(f"{path}: GGUF streaming patch site changed")
        section = section.replace(old, new, 1)
    patch("        bytes_ = engine::io::read_binary_blob(source_path_);", """#if defined(__FreeBSD__) || defined(AUDIOCPP_PS5_STREAM_GGUF)
        file_size_ = std::filesystem::file_size(source_path_);
#else
        bytes_ = engine::io::read_binary_blob(source_path_);
#endif""")
    patch("        bytes_ = engine::io::BinaryBlob();", """        bytes_ = engine::io::BinaryBlob();
        std::vector<std::byte>().swap(tensor_bytes_);""")
    patch("""        if (bytes_.empty()) bytes_ = engine::io::read_binary_blob(source_path_);
        const size_t offset = data_begin_ + info.data_offset;""", """        // ProsperoAI: bounded GGUF tensor reads
#if defined(__FreeBSD__) || defined(AUDIOCPP_PS5_STREAM_GGUF)
        constexpr size_t max_tensor_bytes = 1024ull * 1024 * 1024;
        if (data_begin_ > file_size_ || info.data_offset > file_size_ - data_begin_)
            throw std::runtime_error("GGUF tensor offset is out of bounds: " + info.logical_name);
        const size_t offset = data_begin_ + info.data_offset;
        if (info.byte_size > file_size_ - offset || info.byte_size > max_tensor_bytes)
            throw std::runtime_error("GGUF tensor exceeds PS5 bounded read budget: " + info.logical_name);
        FILE * file = std::fopen(source_path_.string().c_str(), "rb");
        if (file == nullptr)
            throw std::runtime_error("failed to open GGUF tensor: " + info.logical_name);
        try {
            if (fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0)
                throw std::runtime_error("failed to seek GGUF tensor: " + info.logical_name);
            tensor_bytes_.resize(info.byte_size);
            size_t done = 0;
            while (done < info.byte_size) {
                const size_t chunk = std::min<size_t>(1024 * 1024, info.byte_size - done);
                if (std::fread(tensor_bytes_.data() + done, 1, chunk, file) != chunk)
                    throw std::runtime_error("short read of GGUF tensor: " + info.logical_name);
                done += chunk;
            }
        } catch (...) {
            std::fclose(file);
            throw;
        }
        std::fclose(file);
        return {tensor_bytes_.data(), info.byte_size};
#else
        if (bytes_.empty()) bytes_ = engine::io::read_binary_blob(source_path_);
        const size_t offset = data_begin_ + info.data_offset;""")
    patch("        return {bytes_.data() + static_cast<std::ptrdiff_t>(offset), info.byte_size};", """        return {bytes_.data() + static_cast<std::ptrdiff_t>(offset), info.byte_size};
#endif""")
    patch("    mutable engine::io::BinaryBlob bytes_;", """    size_t file_size_ = 0;
    mutable std::vector<std::byte> tensor_bytes_;
    mutable engine::io::BinaryBlob bytes_;""")
    path.write_text(source[:begin] + section + source[end:])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "build/audio-cpp-upstream")
    source = parser.parse_args().source.resolve()
    if not (source / "CMakeLists.txt").is_file():
        raise SystemExit(f"{source}: missing audio.cpp CMakeLists.txt")
    revision = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if revision != PIN:
        raise SystemExit(f"{source}: expected {PIN}, found {revision}")

    cmake = source / "CMakeLists.txt"
    omit_cmake_sources(cmake)
    include_sa3_dependencies(cmake)
    include_ace_step_dependencies(cmake)
    adapt_ace_step_ggml_round(source)
    replace_once(
        cmake,
        "add_library(audiocpp SHARED src/capi/audiocpp.cpp)",
        "add_library(audiocpp STATIC src/capi/audiocpp.cpp)",
    )
    # audio.cpp's public API and model loader are called under ProsperoAI's
    # single inference mutex. Avoid C++ TLS destructors on the PS5 HTTP worker:
    # each API request runs on a short-lived ScePthread and TLS teardown after
    # sending a successful audio response crashes the title on FW 12.70.
    replace_once(
        source / "src/capi/audiocpp.cpp",
        "thread_local std::string g_last_error;",
        "static std::string g_last_error; // PS5: calls are serialized by the app router",
    )
    replace_once(
        source / "src/framework/model_spec/package.cpp",
        """thread_local std::optional<std::filesystem::path> active_model_spec_override;
thread_local std::optional<std::filesystem::path> active_model_path;
thread_local bool active_embedded_spec_checked = false;
thread_local std::optional<assets::GgufEmbeddedModelSpec> active_embedded_spec;
thread_local std::unordered_set<std::string> legacy_embedded_contract_warnings;""",
        """// PS5: model loading is serialized by ProsperoAI's inference mutex; keep
// this cache out of short-lived HTTP worker TLS to avoid unsupported destructors.
static std::optional<std::filesystem::path> active_model_spec_override;
static std::optional<std::filesystem::path> active_model_path;
static bool active_embedded_spec_checked = false;
static std::optional<assets::GgufEmbeddedModelSpec> active_embedded_spec;
static std::unordered_set<std::string> legacy_embedded_contract_warnings;""",
    )
    replace_once(
        source / "src/framework/modules/linear_module.cpp",
        """    if (config_.cuda_nvfp4_f16_activation_lowering) {
        ggml_mul_mat_set_lowering(projected_raw, GGML_MUL_MAT_LOWERING_CUDA_NVFP4_F16_ACTIVATION);
    }
    if (config_.vulkan_f32_activation_lowering) {
        ggml_mul_mat_set_lowering(projected_raw, GGML_MUL_MAT_LOWERING_VULKAN_F32_INPUTS);
    }""",
        "    // The pinned ProsperoAI ggml predates the optional backend-specific lowering hints.",
    )
    replace_once(
        source / "src/framework/modules/structural_modules.cpp",
        """    if (config_.cuda_contiguous_4d_lowering) {
        ggml_concat_set_lowering(raw, GGML_CONCAT_LOWERING_CUDA_CONTIGUOUS_4D);
    }
""",
        "",
    )
    replace_once(
        source / "src/framework/modules/conv_modules.cpp",
        """    // accumulate-in-place GEMM only where the tensor-core mm kernel applies
    // (mirrors the Metal supports gate); otherwise keep the mul_mat + add chain
    const bool use_acc = in_channels >= 64 && output_frames > 8;
""",
        "",
    )
    replace_once(
        source / "src/framework/modules/conv_modules.cpp",
        """        } else if (use_acc) {
            acc = ggml_mul_mat_acc(ctx.ggml, tap_weights, columns, acc);
        } else {""",
        "        } else {",
    )
    replace_once(
        source / "src/framework/modules/primitive_modules.cpp",
        """    return core::wrap_tensor(
        ggml_rope_interleaved_pairs(ctx.ggml, even.tensor, odd.tensor, cos.tensor, sin.tensor),
        output_shape,
        even.type);""",
        """    (void) output_shape;
    throw std::runtime_error("RopeInterleavedPairs is unavailable in the pinned ProsperoAI ggml");""",
    )
    for relative in (
        "src/framework/assets/tensor_source.cpp",
        "src/framework/model_spec/package.cpp",
        "src/framework/assets/resource_bundle.cpp",
        "src/framework/runtime/model.cpp",
        "src/framework/io/filesystem.cpp",
    ):
        adapt_weak_canonical(source / relative)
    tensor_source = source / "src/framework/assets/tensor_source.cpp"
    adapt_gguf_streaming(tensor_source)
    sidecar_root_patch = '''#if defined(__FreeBSD__)
    const auto root = std::filesystem::path("/download0/audiocpp-gguf") / fingerprint.str();
#else
    const auto root = std::filesystem::temp_directory_path() / "audiocpp-gguf" / fingerprint.str();
#endif'''
    tensor_text = tensor_source.read_text()
    if sidecar_root_patch not in tensor_text:
        if 'canonical.parent_path() / ".audiocpp-gguf"' in tensor_text:
            replace_once(
                tensor_source,
                'const auto root = canonical.parent_path() / ".audiocpp-gguf" / fingerprint.str();',
                sidecar_root_patch,
            )
        else:
            replace_once(
                tensor_source,
                'const auto root = std::filesystem::temp_directory_path() / "audiocpp-gguf" / fingerprint.str();',
                sidecar_root_patch,
            )
    registry = source / "src/framework/runtime/registry.cpp"
    replace_once(
        registry,
        "#include \"engine/models/marblenet_vad/session.h\"\n#include \"engine/models/silero_vad/session.h\"\n",
        "",
    )
    replace_once(
        registry,
        """        engine::models::silero_vad::make_silero_vad_loader(),
        engine::models::marblenet_vad::make_marblenet_vad_loader(),
""",
        "",
    )
    print(f"{source}: prepared narrow SA3 build at {PIN}")


if __name__ == "__main__":
    main()
