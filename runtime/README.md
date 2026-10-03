# Clean-room runtime shim

`libc.prx` is generated locally from the independently authored source in this
repository and is distributed under GPL-3.0-or-later. It contains no Sony
runtime implementation, proprietary SDK binary, or game file.

The release artifact has SHA-256:

```text
52fde33426885a3dda5d02ca76ba7f9a720e8f7b165a0293c121bd808aaf10f4
```

> [!WARNING]
> This digest was regenerated after adding the `libSceNet` import library for
> the HTTP API/web UI (see `src/http_server.cpp`). It has not yet been
> confirmed booting on PS5 hardware -- only build determinism is guaranteed
> so far.

Generate it from the repository root:

```sh
make libc
```

Then verify it from this directory with:

```sh
sha256sum -c libc.prx.sha256
```

The generated file is ignored by Git. Bare `make` also creates it as part of a
normal application build. Tagged GitHub Releases provide the verified binary
as a convenience asset.

The complete source, reproduction procedure, and compatibility scope are in
[`tooling/native`](../tooling/native) and
[`docs/RUNTIME_SHIM.md`](../docs/RUNTIME_SHIM.md).
