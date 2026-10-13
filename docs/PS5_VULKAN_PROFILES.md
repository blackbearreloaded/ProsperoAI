# PS5 Vulkan tuning profiles

ProsperoAI creates `/data/prosperoai/vulkan_tuning_profile.txt` on first start
with `ps5` as its default. Edit that persistent file and put one profile name in
it to choose the Vulkan tuning profile:

| File contents | Profile | Settings |
| --- | --- | --- |
| `ps5` | Standard PS5 | Verified base PS5 settings; default |
| `ps5pro` | PS5 Pro | Experimental Pro settings |

For example, to select the Pro profile, replace the file contents with:

```
ps5pro
```

Save the file and restart ProsperoAI for the change to take effect. The `ps5pro`
profile selects the four subgroup mat-vec workgroup, subgroup size 32, and
batch/ubatch 512. The `ps5` profile remains the default. PS5 Pro settings have
not been verified on PS5 Pro hardware; on a base PS5 they were slower than the
standard profile. `/app0/batch_size.txt` and `/app0/ubatch_size.txt` can override
the batch defaults for experiments.

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
