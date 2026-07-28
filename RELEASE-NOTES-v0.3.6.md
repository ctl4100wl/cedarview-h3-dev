# CedarView v0.3.6

This release adds a real WSL2-to-H3 cross-build path. Compilation runs on an
amd64 Debian WSL2 host and produces a verified ARMv7 hard-float executable for
the Vinabox X6 Pro.

## Cross-build system

- Adds a CMake ARMv7/armhf toolchain using `arm-linux-gnueabihf-g++`.
- Uses Debian multiarch Qt 6 and GStreamer target development packages.
- Runs Qt `moc`, `uic`, and `rcc` as native amd64 host tools.
- Isolates the cross-build in `build-wsl-armhf`.
- Rejects a host compiler cached in the ARM build directory.
- Verifies `ELF 32-bit` and `Machine: ARM` before reporting success.
- Stages the checked executable at `dist/cedarview-armhf`.
- Adds repeat-build validation.
- Adds guarded deployment to the `x6pro` SSH target.

## Playback behavior

Playback and MPV failure capture are unchanged from v0.3.5.
