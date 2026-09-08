# Notices

ProsperoAI is built on the
[PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
and uses the public [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).
The clean-room application runtime and project-owned source are distributed
under GPL-3.0-or-later.

The user interface includes RmlUi, SDL2, FreeType, and their retained upstream
license material. The model execution paths incorporate code or static build
artifacts derived from llama.cpp, stable-diffusion.cpp, ggml, espeak-ng,
Kokoro, and the Stable Audio compatibility work in a8nova/adreno-llms. Those
projects retain their respective copyrights and licenses.

No model weights are included in this repository or its automated release.
Curated model repositories record the exact upstream source, preparation
recipe, integrity hashes, and model-specific license:

- [Mistral 7B Instruct v0.3 Q4_0](https://huggingface.co/blackbearreloaded/ProsperoAI-Mistral-7B-Instruct-v0.3-Q4_0-PS5)
- [Qwen3.5 9B Q4_0](https://huggingface.co/blackbearreloaded/ProsperoAI-Qwen3.5-9B-Q4_0-PS5)
- [SD-Turbo FP16](https://huggingface.co/blackbearreloaded/ProsperoAI-SD-Turbo-FP16-PS5)
- [Stable Audio Open Small FP16](https://huggingface.co/blackbearreloaded/ProsperoAI-Stable-Audio-Open-Small-FP16-PS5)
- [Kokoro 82M FP16](https://huggingface.co/blackbearreloaded/ProsperoAI-Kokoro-82M-FP16-PS5)

PlayStation and PS5 are trademarks of Sony Interactive Entertainment.
ProsperoAI is independent homebrew software and is not affiliated with or
endorsed by Sony Interactive Entertainment or the model authors.
