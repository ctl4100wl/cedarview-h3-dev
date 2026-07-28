# CedarView v0.3.4

This release adds bounded live-edge recovery on top of the stable v0.3.3
playback and logging path.

## GO LIVE

- Adds one local MPV JSON IPC socket per tile.
- Adds a manual **LIVE** hover button and right-click **GO LIVE** action.
- Sends MPV's `drop-buffers` command without killing MPV or recreating the
  embedded X11 video window.
- Keeps the manual action available when automatic correction is disabled.

## Automatic live-edge correction

- Samples MPV `time-pos` once per second and compares it with a monotonic timer.
- Never reads `demuxer-cache-time`.
- Does not use ONVIF or the camera wall clock.
- Ignores the first eight seconds after opening a stream.
- Requires three readings beyond the delay threshold, or four seconds with no
  media-time progress, before flushing.
- Reopens only the affected feed if progress is still absent six seconds after
  the flush.
- Enforces a 60-second automatic-correction cooldown across the feed restart.
- Records every flush and escalation in the existing per-camera MPV log.

The controller is enabled by default with a 1250 ms threshold. Both settings
are available under **Developer settings…**.

## Optional Ultra Live timing

- Adds an experimental `--untimed=yes` MPV mode for silent RTSP feeds.
- Keeps the mode disabled by default.
- Restarts active MPV tiles only when the timing mode changes.
- Can reduce steady timestamp wait, but should remain off for irregular or
  visibly choppy streams.

## Preserved v0.3.3 behavior

- Existing TCP/UDP choices remain unchanged.
- Persistent per-camera MPV exit logs remain under
  `~/.local/state/cedarview/mpv/`.
- Feed-sync v0.3.2 and automatic ONVIF clock polling remain removed.
