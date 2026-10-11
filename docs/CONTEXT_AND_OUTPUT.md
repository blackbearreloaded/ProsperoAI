# Context and response limits

In Settings > Intelligence, choose a response limit of up to 4096 tokens and a
context window of 2048, 4096, 8192 or 16384 tokens. Both settings are saved.
Native replies still default to 128 tokens. Context defaults to the model-load
configuration (4096 tokens without a context_size.txt override). Choose Model
default to use that configuration.
Existing saved response choices are retained. The browser has the same limits
under its intelligence settings.

The context window holds the prompt, conversation history and response together.
The Vulkan runtime caps context at the model's training window and caps generation
at the space remaining after the prompt. A larger context allocates a larger KV
cache; it does not increase the reply length unless the response limit is also
raised. Native AGC model bundles retain their compiled context limits.

Ollama-compatible requests accept `options.num_ctx` and `options.num_predict`:

```json
{
  "model": "MiniCPM5-2B-Q8_0.gguf",
  "prompt": "Explain how a database works.",
  "options": {"num_ctx": 8192, "num_predict": 2048}
}
```

The OpenAI-compatible chat endpoint accepts `max_tokens` or
`max_completion_tokens` up to 4096. Its optional `context_tokens` extension selects
512-16384 tokens; without it, model-load configuration applies. API defaults remain 512 response tokens. Changing the effective context rebuilds the model
context on the inference thread. Requests at the same effective context size keep the allocated context. `context_size.txt` remains the fallback for clients that omit a context.

Native replies, saved messages and Ollama response text now hold up to 32767
UTF-8 bytes, including native streaming updates. HTTP JSON response
buffers include room for escaping those bytes. Existing conversation files use
the same storage format.

## Engineering measurements, 2026-10-10

MiniCPM5 Q8 on firmware 13.60 generated 1024 tokens with an 8192-token context
and 4096 tokens with a 16384-token context. Reply buffers held 4717 and 20619
bytes. Decode throughput was 93.46 and 89.95 tokens/s. A third request evaluated
10026 prompt tokens with a 16384-token context and generated 32 tokens.
These measurements used the private performance integration with MiniCPM5 and
prefix reuse. This independent controls PR preserves upstream defaults and
is checked separately on the Linux host. See `docs/performance/2026-10-10/context-*`.

## PR #15 integration

Native and browser context controls use the shared saved runtime settings (Auto and
4K–128K), with automatic KV precision and context-only recreation that retains model
weights. Explicit API request overrides from PR #19 remain supported up to 16K and
do not update saved preferences; omitted overrides use the shared setting.
`/app0/context_size.txt` takes developer precedence. Prefix reuse and MiniCPM5 support
are integrated from PR #17/#18; console acceptance is tracked in `VK_ACTIVE.md`.
