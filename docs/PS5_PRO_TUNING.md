# PS5 Pro tuning

Measured on a PS5 Pro, firmware 13.60, with Mistral 7B Instruct v0.3 Q4_0,
context 4096, 256 generated tokens, median of three runs through
`/api/generate`. Base-console behaviour is unchanged unless a switch is set.

## vulkan_env.txt

A `vulkan_env.txt` next to `eboot.bin` holds `KEY=VALUE` lines; `src/main.cpp`
sets them as environment variables at start-up, before any GPU work, and writes
each one to the debug log (`vulkan_env KEY=VALUE`). Lines starting with `#` are
ignored. The app reads the file once: close and reopen it after a change.

Recommended on a PS5 Pro:

```
GGML_VK_PS5_DMMV_WG=1
GGML_VK_PS5_DMMV_SUBGROUP=32
```

together with `batch_size.txt` and `ubatch_size.txt` containing `512`.

## ggml-vulkan switches

`tools/prepare-llama-ps5-vulkan.py` adds these to the pinned `ggml-vulkan.cpp`
(applied by `tools/build-vulkan-runtime.sh`, like `prepare-llama-ps5-io.py`):

| Variable | Effect |
| --- | --- |
| `GGML_VK_PS5_DMMV_WG=0\|1` | mat-vec workgroup: one subgroup (llama.cpp's choice for AMD) or four |
| `GGML_VK_PS5_DMMV_SUBGROUP=32\|64` | subgroup (wave) size of the mat-vec shaders |
| `GGML_VK_PS5_RM_STDQ=n`, `GGML_VK_PS5_RM_KQ=n` | rows per workgroup for legacy quants and K-quants |
| `GGML_VK_PS5_SUBMIT_MFLOP=n` | GPU submission size in million FLOPs (0 = by node count only) |

The device log line `ggml_vulkan: ps5-knobs ...` shows the detected architecture
and the values in use. llama.cpp detects the console GPU as RDNA2 (`arch=3`),
subgroup size 64, size control 32-64.

## Measurements (bare llama.cpp, same pinned source)

| Settings | tokens/s |
| --- | ---: |
| none | 94.0 |
| `DMMV_WG=1` | 97.7 |
| `RM_STDQ=2` | 96.9 |
| `DMMV_SUBGROUP=32` | 95.4 |
| `DMMV_WG=1` + `DMMV_SUBGROUP=32` | **98.9** |
| `DMMV_WG=1` + `RM_STDQ=2` | 96.6 |
| `DMMV_WG=1` + `RM_STDQ=4` | 89.3 |
| `GGML_VK_MAX_NODES_PER_SUBMIT=1000` + `SUBMIT_MFLOP=0` | 91.6 |

With the best pair, per-operation GPU timings (`GGML_VK_PERF_LOGGER=1`) show the
4096x14336 FFN-down mat-vec at ~491 GB/s (from ~426) and 4096x4096 at ~420 GB/s
(from ~370); the 14336x4096 gate/up mat-vec was already at ~495 GB/s.

In the app: 86.9 -> 90.8 tokens/s generation and 692 -> 724 tokens/s prompt
reading (these switches plus batch/ubatch 512, interface drawing every frame).

Only Mistral 7B Q4_0 has been measured. Other quantisations use other mat-vec
shaders and may prefer other values; the base PS5 has not been tested.
