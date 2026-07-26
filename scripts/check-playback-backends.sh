#!/usr/bin/env bash
set -u

check_command() {
  local name="$1"
  if command -v "${name}" >/dev/null 2>&1; then
    printf '  %-18s ready (%s)\n' "${name}" "$(command -v "${name}")"
  else
    printf '  %-18s MISSING\n' "${name}"
  fi
}

check_element() {
  local name="$1"
  if gst-inspect-1.0 "${name}" >/dev/null 2>&1; then
    printf '  %-18s ready\n' "${name}"
  else
    printf '  %-18s MISSING\n' "${name}"
  fi
}

echo "CedarView playback backend check"
echo
echo "Programs:"
check_command mpv
check_command ffmpeg
check_command gst-inspect-1.0

echo
echo "GStreamer:"
check_element playbin
check_element rtspsrc
check_element xvimagesink
check_element ximagesink
check_element v4l2slh264dec
check_element v4l2slh265dec

echo
echo "Cedrus devices:"
for path in /dev/video0 /dev/media0 /dev/dri/renderD128; do
  if [[ -e "${path}" ]]; then
    ls -l "${path}"
  else
    echo "  ${path} MISSING"
  fi
done

echo
grep -i '^Cma' /proc/meminfo 2>/dev/null || true
