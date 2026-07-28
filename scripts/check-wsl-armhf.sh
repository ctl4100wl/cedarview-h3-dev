#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${project_dir}/dist/cedarview-armhf"

"${project_dir}/scripts/build-wsl-armhf.sh"

first_hash="$(sha256sum "${binary}" | awk '{print $1}')"
"${project_dir}/scripts/build-wsl-armhf.sh"
second_hash="$(sha256sum "${binary}" | awk '{print $1}')"

[[ "${first_hash}" == "${second_hash}" ]] || {
  echo "ERROR: Two unchanged builds produced different binaries." >&2
  exit 1
}

echo
echo "Repeat-build check passed."
echo "SHA-256: ${second_hash}"
