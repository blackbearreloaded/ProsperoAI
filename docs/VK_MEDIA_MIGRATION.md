# vulkan-runtime media implementation

All app build aliases now produce `build/vulkan-runtime/<TEST-TITLE-ID>` from the same
Prism llama/ggml source pin and `GGML_MAX_NAME=160`. SD is built from pinned source
against those headers and libraries. Private SD/TTS ggml, AGC inference and its
scratch allocator are removed. RADV uses the console AGC platform driver.

```sh
make vulkan-runtime
CC=clang-18 CXX=clang++-18 bash tools/build-vulkan-runtime.sh host
PROSPERO_REQUIRE_VULKAN_RUNTIME=1 HOST_CXX=clang++-18 make test
```

SD uses Vulkan for diffusion, VAE and parameter storage. Stable Audio and Kokoro
matrix products, attention, linear and selected unary operations use the shared
Vulkan bridge. Selected convolution paths use CPU im2col staging in bounded tiles;
other convolution/LSTM/waveform operations remain CPU operations in this runtime.

The SD compatibility header dispatches accumulation precision to the pinned Prism
matmul/flash-attention APIs and rejects unsupported operations. Constant-initialized
SD TLS declarations preserve the actual upstream thread-local storage. Host recursive
PickleValue portability changes do not alter the PS5 libc++ source.

Pocket TTS was not an active runtime feature and is not introduced by this change.
Voice profiling, upload/allocation optimization, physical-memory measurements and
extended switching are tracked in the subsequent draft PR.

See [runtime](VULKAN_RUNTIME.md) and [measured comparison](MEDIA_VULKAN_COMPARISON.md).
