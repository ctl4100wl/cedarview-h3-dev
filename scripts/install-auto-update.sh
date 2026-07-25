#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
unit_dir="${HOME}/.config/systemd/user"

if ! git -C "${project_dir}" rev-parse --is-inside-work-tree \
  >/dev/null 2>&1; then
  echo "Automatic updates require CedarView to be a Git checkout." >&2
  exit 3
fi

mkdir -p "${unit_dir}"

escaped_project_dir="$(
  printf '%s' "${project_dir}" | sed 's/[&|]/\\&/g'
)"

sed "s|@PROJECT_DIR@|${escaped_project_dir}|g" \
  "${project_dir}/packaging/cedarview-update.service.in" \
  > "${unit_dir}/cedarview-update.service"
install -m 0644 \
  "${project_dir}/packaging/cedarview-update.timer" \
  "${unit_dir}/cedarview-update.timer"

systemctl --user daemon-reload
systemctl --user enable --now cedarview-update.timer

echo
echo "Automatic CedarView update checks are enabled."
echo "The box checks roughly every six hours and only builds when Git changed."
echo "Check status with:"
echo "  systemctl --user status cedarview-update.timer"

