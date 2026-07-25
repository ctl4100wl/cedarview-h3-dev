#!/usr/bin/env bash
set -euo pipefail

# Give the graphical session and wired network a moment to settle after
# LightDM autologin. CedarView then enters fullscreen without xdotool.
sleep 5
exec "${HOME}/.local/bin/cedarview" --fullscreen
