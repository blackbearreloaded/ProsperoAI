# Tests

The release gate validates source metadata and assets, reproduces the clean-room
runtime, and performs a complete cross-build. PS5 model behavior is validated
on hardware because the native AGC runtimes cannot execute on the host.

The opt-in [Vulkan prompt-prefix regression](../docs/LLM_PREFIX_REUSE.md)
runs real llama.cpp CPU inference under ASan/UBSan and compares cached and cold
outputs, continued chats, truncation and failure recovery.
