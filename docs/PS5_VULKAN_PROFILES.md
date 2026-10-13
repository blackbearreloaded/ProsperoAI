# PS5 Vulkan tuning profiles

ProsperoAI currently defaults to the verified `ps5` profile. To select the
experimental PS5 Pro settings, create `/app0/vulkan_tuning_profile.txt`
containing:

```
ps5pro
```

Restart the app after changing the profile. The `ps5pro` profile selects the
measured four subgroup mat-vec workgroup, subgroup size 32, and batch/ubatch 512.
`/app0/batch_size.txt` and `/app0/ubatch_size.txt` override those batch defaults.
The PR #29 values have not been measured on the base PS5, so they are not enabled
by default there.

For experiments, `/app0/vulkan_env.txt` may contain one `KEY=VALUE` per line.
Supported ggml Vulkan tuning keys are:

| Key | Values | Effect |
| --- | --- | --- |
| `GGML_VK_PS5_DMMV_WG` | `0` or `1` | Select subgroup or large mat-vec workgroup |
| `GGML_VK_PS5_DMMV_SUBGROUP` | Device-supported size, commonly `32` or `64` | Mat-vec subgroup size |
| `GGML_VK_PS5_RM_STDQ` | `1`–`1024` | Rows per workgroup for standard quantizations |
| `GGML_VK_PS5_RM_KQ` | `1`–`1024` | Rows per workgroup for K-quants |
| `GGML_VK_PS5_SUBMIT_MFLOP` | `0` or a non-negative integer | GPU submission threshold in million FLOPs |

These overrides apply after the profile defaults. Invalid values are ignored by
the backend. Vulkan logs report the detected architecture and effective subgroup
and row settings. Measure each model and quantization separately before keeping
an override; the PS5 Pro result in PR #29 covers Mistral 7B Q4_0 only.
