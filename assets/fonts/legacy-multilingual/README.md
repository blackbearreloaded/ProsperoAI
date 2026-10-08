# The last-resort bitmap font

`Radio-24.fnt` and its three `.rta` pages are the 24-pixel multilingual bitmap font of the
first (RmlUi) interface, made from Source Han Sans SC, Noto Sans and DejaVu Sans; see
`licenses/`.

The interface no longer draws with it. `tools/convert-ui-font.py` keeps only the glyphs no
other face covers (Arabic, Hebrew, Thai, Devanagari, phonetic and other signs: about 2,100
of its 17,854), packs them into a small `.huifont`, and `src/font_set.cpp` loads that as the
last fallback, so text in those scripts is still drawn as it was before: unshaped.
