#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"${project_dir}/scripts/build.sh"

install_prefix="${HOME}/.local"
mkdir -p "${install_prefix}/bin"

cmake --install "${project_dir}/build" --prefix "${install_prefix}"
ln -sfn "${project_dir}/scripts/update.sh" \
  "${install_prefix}/bin/cedarview-update"

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
