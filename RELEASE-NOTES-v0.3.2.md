# CedarView v0.3.2

## Feed synchronization

- Added a per-tile MPV JSON IPC connection.
- Samples `time-pos` and `demuxer-cache-time` every two seconds.
- Compares media progress with Linux system time.
- Detects both stalled playback and a growing gap behind the buffered RTSP
  live edge.
- Requires two consecutive threshold violations before acting.
- Refreshes only the feed that is out of sync.
- Resets the watchdog cleanly on Pause, Close, Main/Sub switching, backend
  changes, manual Retry, and application shutdown.
- Default drift threshold is 2.5 seconds and is configurable from Developer
  settings.

## ONVIF clock checks

- Added `GetSystemDateAndTime` probing through
  `http://CAMERA_IP:80/onvif/device_service`.
- Supports HTTP authentication challenges using the saved camera login.
- Compensates for half of the HTTP round-trip time when calculating offset.
- Shows camera-clock offset in the hover badge when it exceeds one second.
- Keeps camera-clock warnings separate from playback resync decisions.

## Compatibility

- Existing configuration files migrate automatically.
- Every camera defaults to ONVIF port 80.
- The sync watchdog is MPV-only. GStreamer playback and the v0.3.1
  X11-safe compact overlay remain unchanged.
