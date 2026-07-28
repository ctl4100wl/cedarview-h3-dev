#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build"

if grep -qi wsl2 /proc/sys/kernel/osrelease &&
   [[ "$(dpkg --print-architecture 2>/dev/null || true)" == "amd64" ]]; then
  exec "${project_dir}/scripts/build-wsl-armhf.sh"
fi

cmake -S "${project_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${HOME}/.local"
cmake --build "${build_dir}" --parallel

echo
echo "Built ${build_dir}/cedarview"
