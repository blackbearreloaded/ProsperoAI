# Supported models

These curated text models use the shared llama.cpp GGUF runtime. They do not need a
model-specific inference adapter; the runtime selects the correct implementation from the
GGUF architecture metadata.

| Model | Format | Status |
| --- | --- | --- |
| [Qwen3.8 9B Distill Q4_K_M](https://huggingface.co/empero-ai/Qwen3.8-9B-Distill-GGUF) | GGUF | Curated preset; PS5 verification pending |
| [Granite 4.0 H-Tiny Q4_K_M](https://huggingface.co/ibm-granite/granite-4.0-h-tiny-GGUF) | GGUF | Verified on PS5 with three warm benchmark runs |
| [BoHF 12B MoE 3A Q3_K_M](https://huggingface.co/theprint/Bohf-12B-MoE-3A-GGUF) | GGUF | Verified on PS5; 48-token warm response at about 66 tokens/s; experimental model |
| [MiniCPM5 2B Q8_0](https://huggingface.co/openbmb/MiniCPM5-2B-GGUF) | GGUF | Verified on PS5, including generation at the 32K context boundary |

This page lists the curated and verified presets, not every model architecture supported by
the underlying llama.cpp revision. Other GGUFs may work when their architecture and tensor
types are supported by the bundled runtime, but they are not verified presets. Image, audio,
and voice models use their modality adapters and are listed separately in the main README.

Z-Image Turbo is present in the image catalog, but its current PS5 preset has not passed an
end-to-end generation test. Its detailed follow-up report was removed from this PR.
