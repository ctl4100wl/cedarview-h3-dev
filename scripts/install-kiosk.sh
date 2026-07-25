#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Run this script as the desktop user, not as root." >&2
  exit 1
fi

if ! command -v lightdm >/dev/null 2>&1; then
  echo "LightDM is not installed. Install or restore LightDM first." >&2
  exit 2
fi

cedarview_binary="${HOME}/.local/bin/cedarview"
if [[ ! -x "${cedarview_binary}" ]]; then
  echo "Install CedarView first with ./scripts/install-app.sh." >&2
  exit 3
fi
kiosk_launcher="${HOME}/.local/bin/cedarview-kiosk"
if [[ ! -x "${kiosk_launcher}" ]]; then
  echo "Kiosk launcher is missing. Re-run ./scripts/install-app.sh." >&2
  exit 4
fi

autostart_dir="${HOME}/.config/autostart"
mkdir -p "${autostart_dir}"
{
  echo "[Desktop Entry]"
  echo "Type=Application"
  echo "Name=CedarView Camera Wall"
  echo "Comment=Start CedarView fullscreen after desktop login"
  echo "Exec=${kiosk_launcher}"
  echo "Terminal=false"
  echo "X-GNOME-Autostart-enabled=true"
} > "${autostart_dir}/cedarview-kiosk.desktop"
chmod 600 "${autostart_dir}/cedarview-kiosk.desktop"

lightdm_config="$(mktemp)"
trap 'rm -f "${lightdm_config}"' EXIT
{
  echo "[Seat:*]"
  echo "autologin-user=$(id -un)"
  echo "autologin-user-timeout=0"
} > "${lightdm_config}"

sudo install -d -m 755 /etc/lightdm/lightdm.conf.d
sudo install -m 644 "${lightdm_config}" \
  /etc/lightdm/lightdm.conf.d/50-cedarview-autologin.conf

echo
echo "CedarView kiosk startup is installed."
echo "LightDM will log in $(id -un) automatically."
echo "CedarView will start fullscreen inside that graphical session."
echo "Reboot when ready: sudo reboot"
