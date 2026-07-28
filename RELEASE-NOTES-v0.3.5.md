# CedarView v0.3.5

This release fixes MPV failure reporting without changing the RTSP transport,
decoder, buffering, or bounded live-edge policy introduced in v0.3.4.

## Exact MPV failure capture

- Replaces `--no-terminal`, which completely silenced MPV stdout and stderr,
  with captured terminal output and disabled terminal input.
- Keeps console status chatter suppressed while preserving warnings and errors.
- Adds module names and monotonic timestamps to MPV diagnostic messages.
- Records MPV's structured JSON IPC `end-file` reason and `file_error`.

## Accurate live state

- Marks a tile live only after MPV emits `file-loaded`.
- No longer treats a process that merely survived 1.8 seconds as a working
  video stream.

## Unchanged playback behavior

- Keeps each camera's existing TCP or UDP setting.
- Keeps `--hwdec=auto`, the existing output fallback, and cache settings.
- Keeps v0.3.4 LIVE flushing, monotonic drift tracking, startup warmup,
  restart escalation, and correction cooldown unchanged.
