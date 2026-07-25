# CedarView

CedarView is a lightweight native Linux RTSP camera viewer built for small ARM
boards, with the Allwinner H3 running Armbian as the initial target.

This is an independent application. It does not use or redistribute Imou
application code, cloud APIs, logos, or proprietary assets.

## Current MVP

- Native Qt 6 Widgets interface
- GStreamer RTSP playback
- Simple Imou/Dahua setup using camera IP, username, and password
- Automatic RTSP URL generation
- Add, edit, and remove cameras
- RTSP-over-TCP option and adjustable latency
- Custom grids from 1×1 through 5×5
- Assign a camera to any selected tile
- Persistent camera and layout configuration
- `glimagesink` with automatic `ximagesink` fallback
- Private `0600` configuration file

The application can display up to 25 tiles, but that does not mean the H3 can
decode 25 streams. A realistic H3 target is:

- one 1080p H.264 main stream, or
- four low-resolution H.264 sub-streams in a 2×2 grid.

Exact performance depends on whether Cedrus/V4L2 hardware decoding is active.

## Build on Armbian

```bash
chmod +x scripts/*.sh
./scripts/install-armbian.sh
./scripts/h3-video-diagnostics.sh
./scripts/install-app.sh
~/.local/bin/cedarview
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

1. camera IP
2. username
3. password
4. main stream or sub-stream

CedarView generates this internally:

```text
rtsp://USERNAME:PASSWORD@CAMERA_IP:554/cam/realmonitor?channel=1&subtype=1
```

Use the sub-stream for a 2×2 H3 grid. Use the main stream for a single-camera
view.

Before testing CedarView, the same generated URL can be tested with:

```bash
gst-launch-1.0 playbin \
  uri='rtsp://USERNAME:PASSWORD@CAMERA_IP:554/...' \
  video-sink=glimagesink
```

If the video is black or the desktop is unstable, try:

```bash
gst-launch-1.0 playbin \
  uri='rtsp://USERNAME:PASSWORD@CAMERA_IP:554/...' \
  video-sink=ximagesink
```

To make CedarView prefer the fallback permanently, change `"videoSink"` in:

```text
~/.config/MindLab/CedarView/config.json
```

Accepted values are `auto`, `glimagesink`, and `ximagesink`.

## H3 performance checklist

1. Use wired Ethernet.
2. Set cameras to H.264; avoid H.265 on the first test.
3. Use sub-streams around 640×360 or 704×576, 10–15 FPS.
4. Keep each sub-stream near 512–1024 kbit/s.
5. Run `scripts/h3-video-diagnostics.sh`.
6. Check `GST_DEBUG=2 ./build/cedarview` for decoder selection.
7. Avoid compositing effects and desktop scaling.

If GStreamer selects `avdec_h264`, decoding is occurring on the CPU. Look for a
V4L2 stateless/request decoder such as `v4l2slh264dec`; the exact factory name
depends on the Armbian kernel and GStreamer build. `run-h3.sh` raises the rank
of `v4l2slh264dec` automatically when that factory is available.

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
- CedarView currently stores the RTSP URL in a file readable only by the
  current Linux user. A future release should integrate Secret Service/KWallet.
