#!/usr/bin/env bash
# ps5-native-app-boilerplate - Linux/WSL clean-room runtime reproducer.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuilds twice and installs only the byte-identical recorded artifact.
#
# NOTE: expected_raw/expected_signed below were regenerated after adding the
# libSceNet import library (sceNetSocket/bind/listen/accept/send/recv/...)
# needed for the HTTP API and web UI. This libc.prx has NOT been verified on
# PS5 hardware yet -- only build-determinism (two independent builds produce
# byte-identical output) is guaranteed. Anyone re-verifying this on a console
# should update this comment once confirmed working.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work="$root/build/runtime-shim"
native="$root/tooling/native"
expected_raw="a36b123b59049d71dde4b382e6e1c75e09a8c69275b7c9bf1a03ff7d7c5ebddd"
expected_signed="6ca36858662501617123956b7da568103e4ab734ba38ed256be55ca27272e299"
manifest="$root/runtime/libc.prx.sha256"

grep -Fxq "$expected_signed *libc.prx" "$manifest" || {
    echo "runtime checksum manifest does not match the release digest" >&2
    exit 2
}

bash "$root/tools/setup-native-dependencies.sh" --skip-sdk >/dev/null
mkdir -p "$work"
bash "$root/tools/build-host-tools.sh"

for copy in a b; do
    "$root/build/host/libc-builder" "$native/runtime/api-surface.txt" \
        "$native/runtime/imports.txt" "$work/libc-$copy.raw.elf"
done
cmp --silent "$work/libc-a.raw.elf" "$work/libc-b.raw.elf"
raw_hash=$(sha256sum "$work/libc-a.raw.elf" | cut -d ' ' -f 1)
[[ $raw_hash == "$expected_raw" ]] || {
    echo "raw runtime hash mismatch: $raw_hash" >&2
    exit 2
}

for copy in a b; do
    "$root/build/host/ps5-native-tool" self --sign \
        --in "$work/libc-$copy.raw.elf" --out "$work/libc-$copy.prx"
done
cmp --silent "$work/libc-a.prx" "$work/libc-b.prx"
signed_hash=$(sha256sum "$work/libc-a.prx" | cut -d ' ' -f 1)
[[ $signed_hash == "$expected_signed" ]] || {
    echo "signed runtime hash mismatch: $signed_hash" >&2
    exit 2
}
[[ $(stat -c %s "$work/libc-a.prx") == 1284962 ]] || {
    echo "signed runtime size mismatch" >&2
    exit 2
}

for artifact in "$work/libc-a.raw.elf" "$work/libc-a.prx"; do
    grep -aFq BlackBearReloaded "$artifact" || {
        echo "generated runtime is missing its attribution marker" >&2
        exit 2
    }
    for forbidden in W:/Build J013 Prospero_Release sys/internal; do
        if grep -aFq "$forbidden" "$artifact"; then
            echo "generated runtime contains forbidden text: $forbidden" >&2
            exit 2
        fi
    done
done

cp "$work/libc-a.prx" "$root/runtime/libc.prx"
(cd "$root/runtime" && sha256sum -c libc.prx.sha256)
printf 'Rebuilt clean-room runtime.\nRaw SHA-256:    %s\nSigned SHA-256: %s\n' \
    "$raw_hash" "$signed_hash"
