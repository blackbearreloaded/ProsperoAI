# PS5 dependency snapshot

This directory contains the PS5 headers, static archives, and import stubs used
by the validated ProsperoRadio build. They are checked in so a repository clone uses
the same application-facing dependency set.

- SDL2 headers and `libSDL2.a` provide the media decoder's helpers, the input
  clock, and synchronization primitives (the interface itself is drawn with
  OpenGL). The SDL license is retained in `sdl/include/SDL2/SDL_copying.h`.
- C++ runtime archives, unwind support, and public libc/kernel import stubs come
  from the open-source PS5 Payload SDK toolchain.

The compiler and PS5 target support still come from the SDK installed at
`/opt/ps5-payload-sdk` inside WSL. Do not add proprietary Sony SDK files,
firmware modules, extracted game libraries, keys, or credentials here.
