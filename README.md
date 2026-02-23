# lvgl-verdin-imx8mp-app (Option 2 skeleton)

This repo is an **application skeleton** that builds on top of **lvgl/lv_port_linux**.
It keeps LVGL + Linux backends in the upstream repo and only swaps the UI entry point.

Why this structure?
- You keep upstream backends (DRM, fbdev, evdev, etc.) as-is.
- You only maintain your UI in `app/`.
- Your Yocto recipe can fetch this repo and apply the patch in `patches/`.

## Quick start (PC / dev machine)

```bash
./bootstrap.sh
cmake -S external/lv_port_linux -B build -DCONFIG=drm
cmake --build build -j
./build/bin/lvglsim
```

> Use `-DCONFIG=fbdev` or `-DCONFIG=sdl` if you want a different backend.
> The upstream repo supports this config mechanism. citeturn1view1

## Yocto notes (kirkstone / Toradex)

When building in Yocto, you generally want:
- `python3-pcpp-native` (LVGL config preprocessing)
- `libdrm` if using DRM/KMS backend
- `libevdev` if using EVDEV input backend
(as you already discovered during your recipe bring-up)

## What you customize
Edit:
- `app/ui.c` and `app/ui.h`

The patch replaces the default demo call with `app_ui_init()`.

## Important
This zip intentionally **does not** include the full `lv_port_linux` contents.
It is meant to be added as a git repo and fetch `lv_port_linux` as a submodule (or via Yocto `gitsm://`).
