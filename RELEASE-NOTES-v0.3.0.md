# CedarView 0.3.0

This release redesigns the live wall around an Imou-style, feed-first
interaction model while keeping CedarView independent and lightweight.

## Live-view controls

- Hover a feed to reveal its camera name, state, and compact controls.
- Pause or resume an individual feed.
- Switch between Main (`subtype=0`) and Sub (`subtype=1`) without opening the
  camera editor.
- Reconnect immediately or close a feed from the overlay.
- Right-click exposes the same actions.
- Stream selection is saved as the camera's preferred stream.

## Selection and layout

- The selected tile has an orange outline.
- The camera sidebar shows available and active cameras more clearly.
- Drag a playing feed onto another tile to swap their positions.
- Existing sidebar-to-tile drag and assignment behavior remains supported.

## Unobstructed viewing

- Tile controls hide after 2.2 seconds without pointer movement.
- In fullscreen, the cursor and all tile controls hide after 2.5 seconds.
- Moving or clicking the mouse restores them immediately.

## Recovery

- A failed or ended stream retries automatically forever.
- Retry delay increases through 2, 4, 8, and 15 seconds, then remains capped
  at 15 seconds.
- Duplicate backend error signals cannot postpone the scheduled retry.
- Pause, Close, backend switching, clearing a tile, and application shutdown
  do not accidentally reconnect a deliberately stopped feed.
