# CedarView

CedarView is a lightweight native Linux RTSP camera viewer built for small ARM
boards, with the Allwinner H3 running Armbian as the initial target.

This is an independent application. It does not use or redistribute Imou
application code, cloud APIs, logos, or proprietary assets.

## Current MVP

- Native Qt 6 Widgets interface
- Selectable MPV/FFmpeg or native GStreamer playback per tile
- Experimental Cedrus mode using GStreamer's stateless V4L2 H.264/H.265
  decoders
- Simple Imou/Dahua setup using camera IP, username, and password
- Automatic RTSP URL generation
- Asynchronous LAN scan for devices exposing RTSP port 554
- Editable camera-IP dropdown populated by scan results
- TCP or UDP RTSP transport per camera
- 16:9 camera tiles with Fill (center crop) or Fit display modes
- Per-camera 100–200% digital zoom
- Frameless rectangular video wall with no camera-name bezels
- Cached camera snapshots in the sidebar, refreshed sequentially at startup
- Drag cameras from the sidebar directly onto a grid tile
- Drag a playing feed onto another tile to swap their positions
- Imou-style compact hover controls that disappear after mouse inactivity
- X11-safe floating control badges with no full-tile transparent widget
- Per-feed Pause/Resume, immediate reconnect, Close, and Main/Sub switching
- Main/Sub changes persist as the camera's preferred stream
- Automatic reconnect after stream failure with 2–15 second capped backoff
- Persistent per-camera MPV exit logs under
  `~/.local/state/cedarview/mpv/`
- Per-feed **LIVE** button that tells MPV to discard queued frames without
  recreating its X11 window
- Safe automatic live-edge correction based on monotonic media-time progress,
  with an eight-second warmup, repeated-sample confirmation, flush-first
  recovery, and a 60-second cooldown
- Optional **Ultra Live timing** for silent MPV feeds using `--untimed=yes`;
  disabled by default because irregular RTSP streams can become choppy
- Fullscreen idle mode hides both tile overlays and the mouse cursor
- Occupancy-aware camera moves: dragging an already shown camera to another
  tile swaps the two tile assignments instead of duplicating the stream
- Sidebar tile-position indicators
- Persistent light and dark interface modes
- Add, edit, and remove cameras
- Automatic balanced layouts and camera-count presets from 1 through 25
- Centered incomplete rows for odd counts such as 5, 7, and 9
- Assign a camera to any selected tile
- Fullscreen video-wall mode with button, F11, Escape, right-click exit, and
  a `--fullscreen` startup option
- Persistent camera and layout configuration
- XVideo output with plain X11 fallback; no `glimagesink`
- Private `0600` configuration file

The application can display up to 25 tiles, but that does not mean the H3 can
decode 25 streams. A realistic H3 target is:

- one 1080p H.264 main stream, or
- four low-resolution H.264 sub-streams in a 2×2 grid.

Exact performance depends on whether Cedrus/V4L2 hardware decoding is active.

## Playback failure logs

CedarView records MPV warnings and the final exit status separately for every
camera. This avoids losing the real failure when a tile automatically
reconnects:

```bash
tail -n 120 ~/.local/state/cedarview/mpv/camera-*.log
```

Each log rotates after 2 MiB. Reconnect backoff is reset only after the player
has remained alive for 60 seconds.

## Staying near the live edge

Every MPV tile owns a local JSON IPC socket. Once per second CedarView compares
the stream's `time-pos` progress with a monotonic Linux timer. It does not use
camera wall-clock time, ONVIF polling, or `demuxer-cache-time`.

Automatic correction follows a bounded sequence:

1. Ignore the first eight seconds after a stream opens.
2. Require the configured delay threshold for three readings, or confirm that
   media time has stopped for four seconds.
3. Send MPV's `drop-buffers` command to discard queued packets and frames.
4. Keep the existing MPV process and X11 window if media progress resumes.
5. Reopen only that feed if no media progress appears within six seconds.
6. Permit no further automatic correction on that tile for 60 seconds.

Hover over a tile and press **LIVE** to request the same buffer flush manually.
Manual use does not trigger an automatic restart. The automatic controller and
its 750–5000 ms delay threshold are configurable under **Developer settings…**.
Disabling automatic correction leaves the manual **LIVE** button available.

**Ultra Live timing** is a separate experimental option. It asks MPV to output
silent-stream frames without waiting on their timestamps and may reduce steady
latency further. Enabling or disabling it restarts active MPV tiles because it
is a player startup option. Leave it off if a camera has irregular timestamps
or the picture becomes choppy.

## Cross-compile on WSL2 for the X6 Pro

The supported fast build path runs on Debian amd64 under WSL2 and produces a
32-bit ARM hard-float executable for the Allwinner H3. It does not compile on
the X6 Pro and it does not run the ARM compiler under emulation.

One-time setup inside WSL2:

```bash
chmod +x scripts/*.sh
./scripts/setup-wsl-armhf.sh
```

Build and verify locally on WSL2:

```bash
./scripts/build-wsl-armhf.sh
file dist/cedarview-armhf
```

The build script succeeds only if both `file` and ARM `readelf` identify the
output as a 32-bit ARM executable. A second unchanged-build test is available:

```bash
./scripts/check-wsl-armhf.sh
```

With an SSH alias named `x6pro`, build and deploy the checked executable:

```bash
./scripts/deploy-wsl-armhf.sh
```

To use an address instead of the alias:

```bash
./scripts/deploy-wsl-armhf.sh ctl4100wl@192.168.1.19
```

The deploy script refuses non-ARM output and refuses a non-ARM destination.
It installs the binary at `~/.local/bin/cedarview` on the X6 Pro.

## Native build on Armbian

```bash
chmod +x scripts/*.sh
./scripts/install-armbian.sh
./scripts/h3-video-diagnostics.sh
./scripts/install-app.sh
~/.local/bin/cedarview
```

Use **Developer settings…** to switch the persistent playback backend. Changing
it restarts active tiles immediately:

- **MPV / FFmpeg** is the stable default and preserves crop and digital zoom.
- **GStreamer / Cedrus** prefers `v4l2slh264dec` or `v4l2slh265dec`
  automatically after inspecting the stream codec. It uses `xvimagesink` with
  `ximagesink` fallback and embeds each sink in its Qt tile.

Install or verify both backend stacks independently with:

```bash
./scripts/setup-playback-backends.sh
./scripts/check-playback-backends.sh
```

The dependency script targets current Debian/Ubuntu-based Armbian images using
Qt 6. If your image uses an older package set, upgrade the image instead of
mixing repositories.

After installation:

- executable: `~/.local/bin/cedarview`
- update command: `~/.local/bin/cedarview-update`
- desktop launcher: `~/.local/share/applications/cedarview.desktop`
- development build: `CedarView/build/cedarview`

The installation is user-local, so normal app updates do not need `sudo`.

## Fast updates without SCP

Put CedarView in a Git repository once, then clone that repository onto the H3.
Do not repeatedly copy release archives to the box.

One-time H3 setup:

```bash
git clone YOUR_CEDARVIEW_GIT_URL ~/CedarView
cd ~/CedarView
chmod +x scripts/*.sh
./scripts/install-armbian.sh
./scripts/install-app.sh
```

Every later manual update is one command:

```bash
cedarview-update
```

That command:

1. fetches only Git changes
2. refuses to overwrite local edits
3. fast-forwards the checkout
4. runs an incremental Ninja build
5. installs the new executable into `~/.local/bin`

To check automatically about every six hours:

```bash
cd ~/CedarView
./scripts/install-auto-update.sh
```

The timer performs a lightweight Git fetch. It compiles only when a new commit
exists. The running CedarView process continues safely on its existing code and
uses the update after the app is restarted.

## Add an Imou/Dahua RTSP camera

Click **Add** and enter:

1. camera IP, selected from **Scan LAN** results or typed manually
2. username
3. password
4. main stream or sub-stream
5. TCP or UDP
6. Fill or Fit sizing and optional digital zoom

CedarView generates this internally:

```text
rtsp://USERNAME:PASSWORD@CAMERA_IP:554/cam/realmonitor?channel=1&subtype=1
```

Use the sub-stream for a 2×2 H3 grid. Use the main stream for a single-camera
view.

At startup, CedarView displays the last cached snapshot immediately and asks
mpv to refresh one camera thumbnail at a time. This avoids launching a burst of
extra decoders on the H3. Drag a snapshot from the sidebar onto any tile to
place or replace that feed. If the camera is already on the wall, CedarView
moves it to the new tile and moves the displaced camera back to the old tile.
The sidebar shows each camera's current tile number. You can also drag a live
tile directly onto another live tile to swap them.

Move the pointer over a feed to reveal its compact overlay:

- **Ⅱ / ▶** pauses or resumes only that feed
- **MAIN / SUB** switches `subtype=0` and `subtype=1` immediately
- **↻** reconnects immediately
- **LIVE** discards queued frames and jumps toward the current live edge
- **×** closes the feed and clears its tile

The overlay disappears after 2.2 seconds without movement. In fullscreen the
mouse cursor also disappears after 2.5 seconds and returns on the next mouse
movement. Right-click exposes the same controls plus **Exit fullscreen**.

When mpv exits, GStreamer reports an error, or RTSP reaches end-of-stream,
CedarView does not leave the feed stuck at Offline. It retries forever with a
backoff of 2, 4, 8, then 15 seconds (capped at 15 seconds). Pause and Close
cancel retrying; Resume and Reconnect now restart immediately.

Choose **Auto — use camera count** to recalculate the wall whenever cameras are
added or removed. Camera-count presets use balanced rows and center an
incomplete last row: 5 cameras become 3+2, 7 become 4+3, and 9 become 3×3.

Click **Fullscreen** or press **F11** to hide the sidebar and status bar. Press
**Escape**, or right-click any feed and choose **Exit fullscreen**, to return.
Start directly in fullscreen with:

```bash
~/.local/bin/cedarview --fullscreen
```

## Automatic login camera-wall mode

Keep LightDM installed: it creates the X11 desktop session required by Qt and
mpv. After installing CedarView, enable automatic login and delayed fullscreen
startup with:

```bash
./scripts/install-kiosk.sh
sudo reboot
```

The installer writes a small LightDM override for the current user and a
desktop autostart entry. CedarView starts five seconds after login so the
display and wired network can settle. It does not replace the desktop or
LightDM, so Escape or **Exit fullscreen** returns to the normal session.

Before testing CedarView, the same generated URL can be tested directly:

```bash
mpv --no-config \
  --vo=xv,x11 \
  --hwdec=auto \
  --profile=low-latency \
  --rtsp-transport=tcp \
  'rtsp://USERNAME:PASSWORD@CAMERA_IP:554/...'
```

In MPV mode each tile owns a separate `mpv` process attached to its X11
window. In GStreamer mode each tile owns an independent native pipeline and
embedded XVideo sink. Slow RTSP negotiation cannot block Qt's event loop, and
one failed camera does not freeze the other tiles or the camera editor.

**Fill 16:9** is the default and makes the picture as large as possible without
stretching it; excess edges are center-cropped. **Fit 16:9** shows the complete
camera frame and may add black bars. Digital zoom is applied by mpv and does
not alter the camera's stream or consume extra network bandwidth.

The LAN scan is intentionally simple and fast. It checks port 554 on the local
IPv4 `/24` without attempting authentication. A result means an RTSP service
answered; the supplied username and password are still required.

## H3 performance checklist

1. Use wired Ethernet.
2. Set cameras to H.264; avoid H.265 on the first test.
3. Use sub-streams around 640×360 or 704×576, 10–15 FPS.
4. Keep each sub-stream near 512–1024 kbit/s.
5. Run `scripts/h3-video-diagnostics.sh`.
6. Run the direct `mpv` test above before testing a grid.
7. Avoid compositing effects and desktop scaling.

`mpv --hwdec=auto` asks FFmpeg/mpv to select an available mainline hardware
decode path and falls back to software decoding if none can initialize. Video
output tries XVideo first and plain X11 second, avoiding the blank
`glimagesink` path entirely.

## PTZ

The target Imou cameras are ONVIF-compatible, so the planned PTZ module does
not need a reverse-engineered Imou cloud protocol. It can use ONVIF to:

- discover cameras
- authenticate and read media profiles
- request the camera-provided RTSP URI
- continuous pan/tilt/zoom
- stop movement
- presets

The first MVP keeps ONVIF/PTZ separate from playback so a PTZ failure cannot
take down the RTSP grid.

## Security notes

- Prefer a dedicated read-only camera account.
- Keep cameras on a trusted LAN or camera VLAN.
- Do not expose port 554 directly to the internet.
- CedarView currently stores camera credentials in a file readable only by the
  current Linux user. A future release should integrate Secret Service/KWallet.
