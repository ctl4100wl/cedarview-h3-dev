#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if grep -qi wsl2 /proc/sys/kernel/osrelease; then
  echo "ERROR: Do not install the ARM binary into WSL2." >&2
  echo "Build with:  ./scripts/build-wsl-armhf.sh" >&2
  echo "Deploy with: ./scripts/deploy-wsl-armhf.sh" >&2
  exit 1
fi

"${project_dir}/scripts/build.sh"

install_prefix="${HOME}/.local"
mkdir -p "${install_prefix}/bin"

cmake --install "${project_dir}/build" --prefix "${install_prefix}"
ln -sfn "${project_dir}/scripts/update.sh" \
  "${install_prefix}/bin/cedarview-update"
install -m 755 "${project_dir}/scripts/cedarview-kiosk.sh" \
  "${install_prefix}/bin/cedarview-kiosk"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database \
    "${install_prefix}/share/applications" >/dev/null 2>&1 || true
fi

echo
echo "CedarView is installed."
echo "Executable: ${install_prefix}/bin/cedarview"
echo "Updater:    ${install_prefix}/bin/cedarview-update"
echo "Launch it from the desktop application menu."
echo
echo "If ~/.local/bin is in PATH, run: cedarview"
