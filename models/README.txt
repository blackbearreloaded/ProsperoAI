ProsperoAI models

This release intentionally contains no model weights. Download a curated
ProsperoAI model bundle, then copy its entire folder here. Text models use:

  models/<model-id>/model.ps5lm
  models/<model-id>/tokenizer.ps5tok
  models/<model-id>/model.json

Directory-based image and audio bundles keep their prepared weights below the
same model folder and include model.json at its root. The JSON "purpose" and
"runtime" fields select the matching media runtime.

ProsperoAI discovers valid folders automatically at launch. Use Workshop in
the app to switch between installed models. model.json supplies the friendly
name and purpose (text-to-text, text-to-image, text-to-audio, or
text-to-speech). Text-model headers select and validate their GPU backend.
