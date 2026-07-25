#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${project_dir}/build/cedarview"

if [[ ! -x "${binary}" ]]; then
  echo "CedarView is not built yet. Run ./scripts/build.sh first." >&2
  exit 1
fi
if ! command -v mpv >/dev/null 2>&1; then
  echo "mpv is missing. Run ./scripts/install-armbian.sh first." >&2
  exit 2
fi

exec "${binary}" "$@"
