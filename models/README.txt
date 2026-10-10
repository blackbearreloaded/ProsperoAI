ProsperoAI models

This release intentionally contains no model weights. Download a curated
ProsperoAI model bundle, then copy its entire folder to /data/prosperoai/models. Text models use:

  /data/prosperoai/models/<model-id>/model.ps5lm
  /data/prosperoai/models/<model-id>/tokenizer.ps5tok
  /data/prosperoai/models/<model-id>/model.json

Directory-based image and audio bundles keep their prepared weights below the
same model folder and include model.json at its root. The JSON "purpose" and
"runtime" fields select the matching media runtime.

ProsperoAI discovers valid folders automatically at launch. Use Models in
the app to switch between installed models. model.json supplies the friendly
name and purpose (text-to-text, text-to-image, text-to-audio, or
text-to-speech). Text-model headers select and validate their GPU backend.

The Vulkan build discovers GGUF files in the same shared directory and one
subdirectory level. Downloads also go into this shared directory.
