# MiniCPM5 2B Q8_0

Open Models, switch to Text and select MiniCPM5 2B Q8_0 to download the official
OpenBMB GGUF. After installation, select it from the installed model library.
The preset is an optional 2679710688-byte download. Releases contain no weights.
Existing model selection and startup behaviour are unchanged.

The runtime recognises `tokenizer.ggml.pre=minicpm5` and applies OpenBMB's text-chat
format, including one explicit BOS and reasoning blocks in assistant history.
Other models keep their existing template path. MiniCPM5 reasoning tokens count
toward the configured response limit.

## Engineering checks

The pinned GGUF revision is `2079a22f3beaa4e306449978533478fe0522f4b3`, SHA-256
`c5415f8989bf88a8288f1b55a3cc371af53c07b0faa220a63bd7a990cfaba078`.
Host tests compare eight text conversations with the embedded official Jinja
template using Jinja2 3.1.6, and verify preset ordering and integrity metadata.
Recorded oracle outputs also run in the ordinary suite without Jinja.

On PS5 firmware 13.60, this exact GGUF completed the isolated tuning sweep at
median 107.08 decode tokens/s for a 40-token prompt and 102.50 for a 616-token
prompt. The combined native UI/API app measured about 96 tokens/s. These runs
used the private performance integration, not this independent PR branch.
Complete model FTP readback matched the pinned SHA-256.
