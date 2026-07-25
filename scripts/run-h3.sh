#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${project_dir}/build/cedarview"

if [[ ! -x "${binary}" ]]; then
  echo "CedarView is not built yet. Run ./scripts/build.sh first." >&2
  exit 1
fi

# Prefer the H3's stateless V4L2 decoder when the installed GStreamer exposes
# it. If it is absent, playbin will select another available H.264 decoder.
if gst-inspect-1.0 v4l2slh264dec >/dev/null 2>&1; then
  export GST_PLUGIN_FEATURE_RANK="v4l2slh264dec:300${GST_PLUGIN_FEATURE_RANK:+,${GST_PLUGIN_FEATURE_RANK}}"
  echo "Using preferred H3 decoder: v4l2slh264dec"
else
  echo "No v4l2slh264dec factory found; GStreamer will select the decoder."
fi

exec "${binary}" "$@"
