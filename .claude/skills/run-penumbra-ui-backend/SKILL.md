---
name: run-penumbra-ui-backend
description: Build, run, and screenshot penumbra-ui-backend's demo (the Lustre Style Wiring Demo, a native SDL3/Penumbra desktop GUI app). Use when asked to run this repo's demo, start it, take a screenshot of it, verify a Lustre/style-wiring change visually, or interact with the running app.
---

A native SDL3/Penumbra desktop GUI app (no Electron, no browser) that recreates
`docs/lustre_core_spec.md` §4's HealthBar worked example — driven, screenshotted, and
click-simulated entirely by the app itself via built-in env-var hooks (`demo/main.cpp`).
No `xdotool`/`import`/`scrot`/Xvfb needed anywhere in this path: the app screenshots
itself with SDL3's own `SDL_RenderReadPixels`/`SDL_SaveBMP`, and simulates the click by
calling the exact same `PenumbraWidgetAdapter::ApplyPropDiff` path a real mouse click
takes. Drive it via `.claude/skills/run-penumbra-ui-backend/smoke.sh`.

All paths below are relative to the repo root (`penumbra-ui-backend/`).

## Prerequisites

Everything needed was already present in the container this was authored in — no
install command was run this session, so none is claimed here. You need: a C++23
compiler, CMake 3.24+, SDL3 + SDL3_ttf dev headers, and a real X/Wayland `DISPLAY`
(this was verified against `DISPLAY=:0`, a real desktop session — no Xvfb fallback has
been tested; see Gotchas).

## Setup

```bash
git submodule update --init --recursive
```

## Build

```bash
cmake -S . -B build
cmake --build build --target penumbra_ui_backend_demo
```

## Run (agent path)

Use the driver — it builds if needed, launches the demo with its screenshot/auto-click
env vars, converts the BMP output to PNG, and exits automatically:

```bash
.claude/skills/run-penumbra-ui-backend/smoke.sh [output-dir]
# output-dir defaults to /tmp/penumbra-ui-backend-demo-shots
```

This produces two screenshots in `output-dir`: `before.png` (mount-time render, the
green `.bar-normal` state) and `after.png` (after an automated click, the red
`.bar-critical` state) — proving both wiring points
`docs/penumbra_ui_backend_lustre_bridge_decision.md` describes: `Walker.cpp` resolving
style at mount, and `PenumbraWidgetAdapter::ApplyPropDiff` re-resolving it on a class
change. **Look at both PNGs** — a solid black frame means the capture raced the window
not yet being composited (see Gotchas); the driver already works around the one race
that was actually hit, but if you see black frames again, that's the first thing to
suspect.

The underlying env vars, if you need finer control than the driver script gives you
(all optional; unset, the demo runs exactly as it always did — interactive only):

| env var | effect |
|---|---|
| `DEMO_SCREENSHOT_BEFORE=<path>` | write a BMP screenshot ~500ms after launch |
| `DEMO_SCREENSHOT_AFTER=<path>` | simulate a click ~1000ms in, then write a BMP ~1500ms in |
| `DEMO_AUTO_EXIT=1` | quit automatically once every requested screenshot is written |

```bash
DISPLAY=:0 \
DEMO_SCREENSHOT_BEFORE=/tmp/before.bmp \
DEMO_SCREENSHOT_AFTER=/tmp/after.bmp \
DEMO_AUTO_EXIT=1 \
  timeout 10 ./build/demo/penumbra_ui_backend_demo
```

## Run (human path)

```bash
DISPLAY=:0 ./build/demo/penumbra_ui_backend_demo
```

Opens a real window. Click anywhere to toggle the health bar's class between
`.bar-normal` (green) and `.bar-critical` (red). Close the window or Ctrl-C to quit.

## Test

```bash
./build/tests/penumbra_ui_backend_tests
```

69 assertions across `WalkerTests.cpp`, `PenumbraWidgetAdapterTests.cpp`,
`SlotWiringTests.cpp`, `LustreStyleApplierTests.cpp`, and `StyleWiringTests.cpp`. All
pass on a clean build as of this writing (`0 failure(s)`).

## Gotchas

- **Screenshot must be captured between `Draw` and `Present`, not after.** Capturing
  right after `Renderer.EndFrameAndPresent()` (which calls `SDL_RenderPresent`)
  intermittently read a solid-black frame — confirmed across repeated runs, roughly
  half of them — because `SDL_RenderReadPixels` raced the backbuffer swap on this
  accelerated/double-buffered renderer. Moving the read to right after `Root->Draw()`
  but *before* `EndFrameAndPresent()` (reading the render target while it definitely
  still holds what was just drawn) made it reliable across 6+ consecutive stress-test
  runs. If you add more screenshot points, keep them in that same window.
- **Wall-clock delay, not frame count, and it needs real time.** An earlier version
  gated the first screenshot on `FrameCount == 5`; it captured pure black because the
  window took longer than 5 loop iterations to actually have real content in its
  backbuffer under a real compositor. The demo now waits on `SDL_GetTicks()` instead
  (500ms before the first shot, 1000ms for the auto-click, 1500ms for the second shot)
  — window size/layout (`GetLogicalWindowSize`) were already correct from frame 1 in
  testing, so this wasn't a layout problem, purely a compositor-timing one.
- **BMP, not PNG, straight out of the app.** SDL3 has `SDL_SaveBMP` built in with no
  extra dependency; there's no equally trivial built-in PNG encoder. The driver
  converts with `ffmpeg` (already present in this environment) if it's on `PATH` —
  falls back to leaving the `.bmp` files if not.
- **No Xvfb tested.** This container already had a real `DISPLAY=:0` (an actual desktop
  session, not headless CI), so Xvfb was never exercised. If you're running this in a
  truly headless environment and `DISPLAY` is unset, `smoke.sh` fails fast with a clear
  message rather than silently hanging — install Xvfb and export a `DISPLAY` pointing
  at it yourself; that combination hasn't been verified here.

## Troubleshooting

- **`smoke.sh` exits immediately with "DISPLAY is unset"**: no X/Wayland display is
  reachable in this shell. Either run where a real desktop session exists, or set up
  Xvfb yourself (untested by this skill — see Gotchas).
- **A screenshot PNG is solid black / much smaller than its sibling**: the
  Draw-before-Present race described in Gotchas. If you've modified `demo/main.cpp`,
  check that any new screenshot capture still happens between `Root->Draw(Renderer)`
  and `Renderer.EndFrameAndPresent()`, not after.
