# Use the PS5 Vulkan model in OpenCode

The Vulkan build exposes `GET /v1/models` and `POST /v1/chat/completions` at
`http://<PS5-IP>:11434/v1`. It supports non-streaming completions and SSE chat
chunks, finish reasons, usage, and `[DONE]`. The existing console layout is unchanged.

## Configure the console

Install the latest app-folder ZIP from the release, or build with `make app-release`.
Launch the app and keep it running while using OpenCode. Use Models to download a
Text preset, or copy a raw GGUF to the shared model directory. Vulkan GGUF and AGC
prepared models are stored in `/data/prosperoai/models`; the runtime
registers them immediately and returns their IDs from `/v1/models`. Use the exact model ID
returned by `/v1/models`.

By default the local API requires no authentication: without an `api_key.txt` file,
every HTTP route (including the older Ollama routes) accepts unauthenticated requests.
The API uses plain HTTP, so only expose it on a network you trust.

To require a bearer key instead, create a file named `api_key.txt` in the deployed
app's root (`/data/homebrew/PPSA99004/api_key.txt` for the release), containing your
key on one line, at most 256 bytes including a trailing newline, and restart the
title to load it. The server then requires `Authorization: Bearer <key>` on all HTTP
routes; the built-in web page does not supply this header, so adding a key breaks it.
Keep keys out of Git and PR descriptions.

The Vulkan context defaults to 4096 tokens. For OpenCode, put `16384` in
`context_size.txt` beside `api_key.txt`, then restart. Supported values are
512–16384, capped by the model's trained context. Larger contexts consume more
GPU memory. The benchmark report used 4096; its buffer figures do not describe 16K.

## Configure OpenCode

Add this to `opencode.json` (adjust the address and model ID). No API key is needed
by default, matching the server's unauthenticated default above:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "provider": {
    "prosperoai": {
      "npm": "@ai-sdk/openai-compatible",
      "name": "PS5 ProsperoAI",
      "options": {
        "baseURL": "http://<PS5-IP>:11434/v1"
      },
      "models": {
        "Mistral-7B-Instruct-v0.3.Q4_0.gguf": {
          "name": "Mistral PS5",
          "limit": {"context": 16384, "output": 512},
          "tool_call": true
        }
      }
    }
  },
  "model": "prosperoai/Mistral-7B-Instruct-v0.3.Q4_0.gguf"
}
```

If you set up `api_key.txt` above, also set `PROSPEROAI_API_KEY` in your local
environment to the same key and add `"apiKey": "{env:PROSPEROAI_API_KEY}"` under
`options`.

This uses OpenCode's [custom OpenAI-compatible provider](https://opencode.ai/docs/providers/#custom-provider).
A direct OpenCode 1.1.36 streaming connection returned `API_OK` on the PS5 test build. A PS5 function-call test called `lookup` with `{"query":"Vulkan"}`, accepted the tool result and produced a final answer.

## Tool calls and limits

Tools use a portable prompt/GBNF fallback: function schemas are included in the
system prompt; grammar constrains the JSON envelope and declared function names.
The server maps this to OpenAI `tool_calls`, including call IDs, and maps tool-result
messages back into the conversation. `tool_choice` supports `auto`, `none`,
`required`, and a named function; `parallel_tool_calls: false` restricts output to
one call. Tools execute in the client, not on the PS5. This is not llama-server's
native per-model Jinja tool implementation. Parameter schemas guide the model;
full JSON Schema argument validation is not implemented.

Ordinary text streams as it is generated. Tool-enabled responses are buffered
until the entire JSON envelope has been validated, then sent as SSE deltas.
Invalid or incomplete tool output returns an error rather than executing a
fabricated call. Model ability to choose useful tools still needs workload testing. In an OpenCode read test with automatic tool choice, Mistral 7B answered with invented file contents instead of calling the tool. A diagnostic proxy forcing `tool_choice=required` on the first OpenCode request did complete the `read` tool and returned the real `PROSPERO_TOOL_OK` file contents. That verifies the protocol roundtrip, not automatic tool selection. API compatibility does not establish coding-agent reliability for this model.

Requests accept up to 128 text messages and 256 KiB including headers. Content
may be a string, null, or an array of text parts. Output is capped at 512 tokens;
`max_tokens` and `max_completion_tokens` are accepted. Temperature defaults to
zero (greedy); values above zero use top-k 40 / top-p 0.95 sampling. Only `n=1`
and text responses are supported; custom stop sequences are rejected. Requests
must use Content-Length; chunked request bodies are rejected. Unknown model IDs
return 404. Concurrent API generations return 503. Connection workers are bounded
and completed threads are joined rather than leaked.

The hybrid release serves text through Vulkan and Image, Audio and Voice through
AGC in the same app. OpenCode's coding workflow uses a text model. Voice generation
and switching back to Vulkan text are verified; Image/Audio generation in the
combined release remains unverified on console.
