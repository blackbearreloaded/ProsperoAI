# Supported models

The Models screen can download each curated preset below. These presets use the runtimes and
adapters already included in ProsperoAI; adding one does not require a model-specific code
change. Presets are pinned to revisions and checked against their expected file sizes and
SHA-256 hashes during download.

| Model | Type | Download | PS5 status |
| --- | --- | --- | --- |
| [Mistral 7B Instruct v0.3 Q4_0](https://huggingface.co/QuantFactory/Mistral-7B-Instruct-v0.3-GGUF) | Text, GGUF | Curated preset | Verified; three correct benchmark runs |
| [Qwen3.8 9B Distill Q4_K_M](https://huggingface.co/empero-ai/Qwen3.8-9B-Distill-GGUF) | Text, GGUF | Curated preset | Verified; three correct benchmark runs |
| [MiniCPM5 2B Q8_0](https://huggingface.co/openbmb/MiniCPM5-2B-GGUF) | Text, GGUF | Curated preset | Verified, including generation at the 32K context boundary |
| [Ternary Bonsai 2 27B PTQ1_0](https://huggingface.co/prism-ml/Ternary-Bonsai-2-27B-gguf) | Text, GGUF | Curated preset | Verified; three correct benchmark runs |
| [Granite 4.0 H-Tiny Q4_K_M](https://huggingface.co/ibm-granite/granite-4.0-h-tiny-GGUF) | Text, GGUF | Curated preset | Verified; three correct benchmark runs |
| [BoHF 12B MoE 3A Q3_K_M](https://huggingface.co/theprint/Bohf-12B-MoE-3A-GGUF) | Text, GGUF | Curated preset | Verified; experimental model |
| [SD-Turbo FP16](https://huggingface.co/blackbearreloaded/ProsperoAI-SD-Turbo-FP16-PS5) | Image | Curated preset | Verified; three valid 512 × 512 images |
| [Realistic Vision v5.1 FP16](https://huggingface.co/SG161222/Realistic_Vision_V5.1_noVAE) | Image | Curated preset | Verified; three valid 512 × 512 images |
| [Stable Audio 3 Small SFX Q8_0](https://huggingface.co/audio-cpp/audio.cpp-gguf/tree/main/Stable-Audio-3-Small-SFX-GGUF) | Sound effects | Curated preset | Verified; three valid stereo WAV generations |
| [ACE-Step 1.5 Turbo Q8_0](https://huggingface.co/audio-cpp/audio.cpp-gguf/tree/main/ACE-Step1.5-GGUF/turbo) | Music | Curated preset | Verified; three valid stereo WAV generations |
| [Kokoro 82M Vulkan](https://huggingface.co/blackbearreloaded/ProsperoAI-Kokoro-82M-FP16-PS5) | Speech | Curated preset | Verified; three valid WAV generations |
| [Chatterbox 500M Q8_0](https://huggingface.co/audio-cpp/audio.cpp-gguf) | Speech, voice cloning | Curated preset | Verified; three valid WAV generations; 19 selectable languages |

The table is the complete list of curated downloadable presets in the current build. ProsperoAI
also lets you download standalone GGUF files. Other GGUF models may work when the bundled
llama.cpp revision supports their architecture and tensor types, but they are not curated or
verified here. Curated media presets include their required model files and use the image,
audio, or speech runtime selected by the app.

Image generation currently writes both PNG and TGA: PNG is the shareable/downloadable output,
while the native conversation preview and archived image use TGA. Both formats are needed by
the current app flow.
