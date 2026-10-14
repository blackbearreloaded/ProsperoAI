#!/usr/bin/env bash
# Source this file to make local console settings available to builds and tools.
prospero_env_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prospero_env_exports=$(python3 "$prospero_env_root/tools/ps5_config.py") || return 1
eval "$prospero_env_exports"
unset prospero_env_exports prospero_env_root
if [[ -z ${PS5_HOST:-} ]]; then
    echo "PS5_HOST is missing. Fill .env.ps5.local from .env.ps5.example or export PS5_HOST." >&2
    return 1
fi
