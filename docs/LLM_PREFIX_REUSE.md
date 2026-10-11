# Reusing evaluated prompt prefixes

Vulkan text inference keeps successfully evaluated tokens for the resident model.
Repeated requests and continued chats reuse their matching prefix, then evaluate
the changed suffix. Model changes, failed decodes, missing cache positions and
rejected tail removal clear the cache. Recurrent and hybrid models use full prefill.
Create `disable_prefix_cache.txt` in the app folder to compare the original path.

Optional model-load files are `context_size.txt` (512-16384), `batch_size.txt` and
`ubatch_size.txt` (32-2048), `threads.txt` and `threads_batch.txt` (1-16), and
`flash_attention.txt` (0 auto, 1 disabled, 2 enabled). Invalid files retain defaults:
4096 context, 256 batch/ubatch, four threads and automatic Flash Attention.
Prefill chunks follow the allocated batch size.

## Engineering measurements, 2026-10-10

On firmware 13.60, Mistral 7B Instruct v0.3 Q4_0 repeated 708-token prefill fell
from median 1529.13 ms to 13.40 ms. Fresh decode stayed near 63 tokens/s. All twelve
greedy outputs matched across baseline and patched builds. Qwen3.5 9B Q4_0 retained
full prefill and about 50 tokens/s. The measured baseline was bd98473028deec7240cbe891341855db3683f056; its Vulkan
LLM source matches public upstream commit 8b4714b before the prefix patch. Both tests used 4096 context, 256 batch/ubatch and four
threads. Receipts record model hashes and the tested isolated runtime builds.

Run the opt-in CPU regression with the pinned llama.cpp static archives:

```sh
LLAMA_HOST_BUILD=/absolute/path/to/llama-build \
LLAMA_TEST_MODEL=/absolute/path/to/model.gguf \
python3 -m unittest discover -s tests -p test_llm_prefix_cache.py -v
```

It compares cold and cached text, continued chats, truncation, model changes and
injected failures under AddressSanitizer and UndefinedBehaviorSanitizer.
