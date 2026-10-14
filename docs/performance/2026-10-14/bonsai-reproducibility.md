# Bonsai upstream port verification

The PS5 report `bonsai-hadamard-no-profile.json` records correct arithmetic,
three coherent identical 94-token explanations, correct Thinking-mode arithmetic,
and a short request with a 16K context allocation on PPSA99023. Warm decode samples
are 13.2605, 13.4333 and 13.2587 tokens/s (median 13.2605). The 16K request is not
a long-context quality test. Profiling was disabled for these measurements.

The subsequent model-switch regression crashed the title, so the report does not
establish complete application regression coverage or a clean final console state.

On 2026-10-14, `tools/prepare-bonsai-llama.py` was applied to a clean Git archive of
the pinned upstream source under `build/bonsai-clean-source-audit/`. A second run
left every source hash unchanged. The resulting model, graph and context headers
and implementation files exactly match the dependency source used for the console
benchmark. This check found and corrected insertion order for model/context fields;
the correction preserves the console-tested layout. Loader differences are template
declaration order and the separate PS5 model-I/O preparation.

Fresh console verification of the latest app and Chatterbox remains pending: the
configured console currently returns `No route to host` or times out on payload,
FTP and control service checks. No reboot was requested or performed.
