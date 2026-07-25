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
echo "mpv:"
if command -v mpv >/dev/null 2>&1; then
  mpv --version | sed -n '1,4p'
  echo
  echo "mpv hardware decoders:"
  mpv --no-config --hwdec=help 2>&1 | sed -n '1,120p'
  echo
  echo "mpv video outputs:"
  mpv --no-config --vo=help 2>&1 | sed -n '1,120p'
else
  echo "  mpv is not installed"
fi

echo
echo "FFmpeg H.264 / DRM / V4L2 decoders:"
ffmpeg -hide_banner -decoders 2>/dev/null |
  grep -Ei 'h264|v4l2|drm' |
  sed -n '1,120p' || true

echo
echo "X11 session:"
echo "  DISPLAY=${DISPLAY:-not set}"
echo "  XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-not set}"

echo
echo "Direct player test:"
echo "  mpv --no-config --vo=xv,x11 --hwdec=auto --profile=low-latency \\"
echo "      --rtsp-transport=tcp 'rtsp://USER:PASSWORD@CAMERA/…'"

