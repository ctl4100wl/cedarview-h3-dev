#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
  SUDO=()
else
  SUDO=(sudo)
fi

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  mpv \
  qt6-base-dev \
  ffmpeg

echo
echo "Dependencies installed. Run: ./scripts/install-app.sh"
