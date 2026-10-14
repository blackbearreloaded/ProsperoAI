# Console service payloads

The repository carries the standard ELFs that `tools/ps5ctl.py` sends. They work without
P5 Manager or a second project checkout. `manifest.json` records their exact sizes,
SHA-256, upstream release/source pins and licenses.

| Service | Bundled file | Listener |
| --- | --- | --- |
| FTP | `zftpd-ps5.elf` (FTP-only v1.6.0) | 2120 |
| Control | `ps5vkctl.elf` (PS5_Vulkan `3f3ee696`) | 9111 |
| Kernel log | `klogsrv-ps5.elf` (v0.9) | 3232 |
| Shell | `shsrv-ps5.elf` (v0.20) | 2323 |

```bash
source tools/ps5-env.sh
python3 tools/ps5ctl.py doctor
python3 tools/ps5ctl.py service restore
python3 tools/ps5ctl.py agent procs
```

`service restore` sends only payloads for unavailable ports. The console's ELF loader
must already be running on port 9021. Payloads are resident services, not app-title
builds. App-specific `lapy.elf` and `self-updater.elf` are generated inside the build's
app folder with that title's identity; do not substitute a helper from another title.
Experimental payloads must likewise live under this repository's `build/` tree and
be passed explicitly to `ps5ctl.py payload PATH.elf`.

The control ELF is built from the pinned source in `tools/deps.json`, with SDK v0.42:

```bash
make deps
PS5_PAYLOAD_SDK="$PWD/.deps/native/ps5-payload-sdk" \
  make -C .deps/mihawk-vulkan-review/payload/ps5vkctl OUTPUT="$PWD/build/ps5-services"
```

This produces `build/ps5-services/ps5vkctl.elf`. The shipped binary's hash stays pinned;
changing it requires an intentional manifest update. The other ELFs are unmodified
upstream release assets. Their corresponding source trees and licenses are linked in
the manifest. A successful local integrity check does not establish runtime compatibility
of a newly advanced payload version; record console verification separately.
