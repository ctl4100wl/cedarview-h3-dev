#!/usr/bin/env bash
set -euo pipefail

if ! grep -qi wsl2 /proc/sys/kernel/osrelease; then
  echo "ERROR: This setup script must run inside WSL2 Debian." >&2
  exit 1
fi

source /etc/os-release
if [[ "${ID:-}" != "debian" ]]; then
  echo "ERROR: This release supports Debian under WSL2." >&2
  exit 1
fi

if [[ "$(dpkg --print-architecture)" != "amd64" ]]; then
  echo "ERROR: The WSL2 host must be Debian amd64." >&2
  exit 1
fi

if ! dpkg --print-foreign-architectures | grep -qx armhf; then
  sudo dpkg --add-architecture armhf
fi

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  file \
  g++-arm-linux-gnueabihf \
  ninja-build \
  openssh-client \
  pkg-config \
  qt6-base-dev:amd64 \
  qt6-base-dev-tools:amd64 \
  qt6-base-dev:armhf \
  libgstreamer1.0-dev:armhf \
  libgstreamer-plugins-base1.0-dev:armhf

echo
echo "WSL2 ARMv7/armhf cross-build dependencies are installed."
echo "Next: ./scripts/build-wsl-armhf.sh"
