# The interface kit

ProsperoAI's interface is built with
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui). The kit is a
build dependency: none of its source, fonts or sounds are kept in this repository.

| File | What it is |
| --- | --- |
| `sources.txt` | The kit sources the app compiles, relative to the kit's `src/` |
| `patches/` | Changes laid over the fetched kit before it is compiled |

`tools/prepare-ui-kit.sh` fetches the pinned commit into `.deps/ui-kit/checkout`, copies
its `src/` to `.deps/ui-kit/stage/src`, applies the patches and prints the staged folder.
The console build, the host preview and the host tests all compile from there.
`tools/bake-fonts.sh` bakes the fonts with the kit's baker, and `tools/build.sh` packages
the kit's sounds, straight from the checkout.

## Moving the pin

Change `commit=` in `tools/prepare-ui-kit.sh`. If a patch no longer applies the script
stops and says which one.

To work on the kit and the app together, point the build at a checkout:

```sh
UI_KIT_DIR=~/ps5-homebrew-ui make app
```

## The patch

`patches/font-fallback.patch` is the one change the app needs that the kit does not have
yet. It belongs upstream; once the kit has it, move the pin and delete the file.

- `gfx::Font::add_fallback(face, texture)`: faces asked, in order, for the code points a
  font lacks. Each glyph is drawn from its own face's atlas with that face's metrics and
  distance-field spread. Without a fallback a font behaves exactly as before.
- `gfx::Font::wrap` breaks a word wider than the line between code points (a long address,
  text in a script that writes no spaces) instead of letting it overflow.

To change it, edit the kit in a checkout and write the patch again:

```sh
git -C ~/ps5-homebrew-ui diff --relative=src > ui-kit/patches/font-fallback.patch
```
