#!/usr/bin/env bash
set -u

echo "CedarView / Allwinner H3 video diagnostics"
echo
echo "Kernel:"
uname -a

echo
echo "Video and DRM devices:"
for path in /dev/video* /dev/media* /dev/dri/*; do
  [[ -e "${path}" ]] && ls -l "${path}"
done

echo
echo "Available H.264 decoders:"
gst-inspect-1.0 2>/dev/null |
  grep -Ei 'v4l2.*h264.*dec|cedrus|h264.*dec' |
  sort -u || true

echo
echo "Candidate video sinks:"
for sink in glimagesink ximagesink kmssink; do
  if gst-inspect-1.0 "${sink}" >/dev/null 2>&1; then
    echo "  [yes] ${sink}"
  else
    echo "  [no]  ${sink}"
  fi
done

echo
echo "OpenGL renderer:"
if command -v glxinfo >/dev/null 2>&1; then
  glxinfo -B 2>/dev/null | grep -E 'OpenGL vendor|OpenGL renderer|OpenGL version' || true
else
  echo "  glxinfo not installed"
fi

echo
echo "Run this test before CedarView:"
echo "  gst-launch-1.0 playbin uri='rtsp://USER:PASSWORD@CAMERA/...' video-sink=glimagesink"

