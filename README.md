# CedarView

CedarView is a lightweight native Linux RTSP camera viewer built for small ARM
boards, with the Allwinner H3 running Armbian as the initial target.

This is an independent application. It does not use or redistribute Imou
application code, cloud APIs, logos, or proprietary assets.

## Current MVP

- Native Qt 6 Widgets interface
- Isolated `mpv` playback process per tile
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
- Occupancy-aware camera moves: dragging an already shown camera to another
  tile swaps the two tile assignments instead of duplicating the stream
- Sidebar tile-position indicators
- Persistent light and dark interface modes
- Add, edit, and remove cameras
- Lightweight preset grid dropdown from 1×1 through 5×5
- Assign a camera to any selected tile
- Fullscreen video-wall mode with F11 and Escape shortcuts
- Persistent camera and layout configuration
- XVideo output with plain X11 fallback; no `glimagesink`
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
The sidebar shows each camera's current tile number. Right-click a feed and
select **Clear tile** to remove its assignment.

Choose the wall layout from the **Grid preset** dropdown. Click **Fullscreen**
or press **F11** to hide the sidebar and status bar; press **Escape** to return.

Before testing CedarView, the same generated URL can be tested directly:

```bash
mpv --no-config \
  --vo=xv,x11 \
  --hwdec=auto \
  --profile=low-latency \
  --rtsp-transport=tcp \
  'rtsp://USERNAME:PASSWORD@CAMERA_IP:554/...'
```

Each tile owns a separate `mpv` process attached to the tile's X11 window.
Slow RTSP negotiation cannot block Qt's event loop, and one failed camera does
not freeze the other tiles or the camera editor.

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
