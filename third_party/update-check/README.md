# Update check and self-update

From [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
at 014791e (GPL-3.0-or-later): `examples/update-check` (`update_check.*`) and
`examples/self-update` (`self_update*.{h,c}`), unchanged. It is the mechanism ProsperoLight,
ProsperoEden and ProsperoTV use. The guides are the boilerplate's `docs/UPDATE_CHECK.md` and
`docs/SELF_UPDATE.md`. `console_curl.*`, which the kit needs, is in `vulkan/net`, where the
model downloader already uses it.

`self_update_paths.h` is ProsperoAI's own: it names the helper, the app's `param.json` and the
sequence file, because with filesystem access the app's folder is not `/app0` and its data is
not in `/download0`. The build puts it in front of `self_update_ps5.c` (`-include`).

The helper the app sends to the console's payload loader is in `third_party/self-update-helper`
(the boilerplate's `examples/self-update-helper`). It reads ZIP archives with
`third_party/miniz` (MIT). Two things differ from the boilerplate's copy:

- its Makefile and test name this repository's folders;
- `title_running` in `updater.cpp` counts a sandbox folder as a running app only when the
  app's own folder is mounted in it (`app0`). A folder left behind after an app ended badly
  would otherwise keep every update waiting until the console restarts.

`tools/build-vulkan-native.sh` builds `self-updater.elf` with the PS5 Payload SDK that the
Lapy helper is built with and puts it in the app's folder. `tests/test_self_update.py` runs
the engine against the helper on the PC (`third_party/self-update-helper/test_self_update.cpp`).

The app asks once per launch, on a thread of its own (`vulkan/update_ps5.cpp`); the interface
shows the offer, the release notes, the progress and a failure (`src/native_ui_update.cpp`),
and closes the app once the update is staged.

For trying it before a release is listed: a build made with `UPDATE_DEV_OFFER=1` takes the
offer from `update-offer.txt` in the app's folder (five lines: new content version, release
name, ZIP on GitHub, SHA-256, size; further lines are the release notes) instead of the
catalog. It skips the catalog's signature: never ship such a build.
