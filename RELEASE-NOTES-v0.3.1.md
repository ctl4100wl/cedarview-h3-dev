# CedarView v0.3.1

## Fixed

- Removed the full-tile transparent Qt hover widget that rendered as an opaque
  black rectangle over native mpv/XVideo windows on X11.
- Hover controls are now individual compact native badges and buttons. No
  overlay widget occupies the center of the camera picture.
- Connection and retry text is now a centered compact badge instead of a
  full-tile translucent label.

## Preserved

- Hover auto-hide and fullscreen cursor auto-hide
- Pause/Resume, Main/Sub, Retry, and Close
- Drag live feeds to swap positions
- Persistent automatic reconnect with capped backoff
