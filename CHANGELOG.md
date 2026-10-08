# Changelog

## Unreleased

- Replaced the RmlUi interface with a native OpenGL one built on ps5-homebrew-ui: Workspace,
  Models and Settings pages, a model library without the eight-model limit, search and
  filters, Midnight and Daylight themes with three accents, an opening sequence, a backdrop
  that follows the model in focus, notices, interface sounds, reduced motion, high contrast
  and a larger reading size.
- The interface kit, its fonts and its sounds are fetched at a pinned commit when the app is
  built; nothing of the kit is kept in this repository. RmlUi, FreeType and the old
  interface's assets are removed.
- Chinese, Japanese and Korean are drawn with scalable Noto Sans faces, loaded when a text
  first needs them; accented Latin, Greek and Cyrillic are part of the interface faces.
- Sending a new message after a failed answer replaces the unanswered one instead of storing
  two user turns in a row.
- The token count, speed and context size of the last answer are shown again, and Settings
  has an About page with the version.
- Moved to ps5-opengl SDK 1.0.1.
- The diagnostic log is off unless the install folder holds `dev/log.txt`.
- Adopted the foundation's parallel incremental Ninja build and ccache-backed
  local/CI compilation workflow.
- Fixed RELRO load-segment alignment for executables whose relocated read-only
  region does not begin at the GOT, with a host regression test.
- Published the deterministic text-model converter, validated Mistral and Qwen
  recipes, recipe-authoring workflow, and model-tool self-test.

## 01.000.000

- Published the complete reproducible ProsperoAI application source.
- Added model-free GitHub Actions builds for the app-folder ZIP.
- Documented curated model installation and measured PS5 performance.
- Added the linked video demonstration.
- Kept the 64 most recent saved sessions ordered by activity.
- Completed application-wide ProsperoAI branding cleanup.

## v0.1.0-alpha.3

- Added curated text, image, general-audio, and speech model support.
- Added persistent multimodal sessions and in-app image/audio presentation.
- Added model switching, loading feedback, USB keyboard input, and conversation scrolling.
