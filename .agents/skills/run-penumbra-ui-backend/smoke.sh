#!/usr/bin/env bash
# Automated driver for penumbra-ui-backend's demo (demo/main.cpp, the Lustre
# Style Wiring Demo). Builds if needed, launches the demo with its built-in
# automation env vars, captures a screenshot before and after an
# auto-triggered click, converts them to PNG, and exits automatically.
#
# No xdotool/import/scrot/Xvfb anywhere in this path -- the demo screenshots
# itself via SDL3's own SDL_RenderReadPixels/SDL_SaveBMP (no extra library),
# and drives the click by calling the exact same
# PenumbraWidgetAdapter::ApplyPropDiff path a real mouse click takes, from
# inside the app itself. See demo/main.cpp's own doc comment above the env
# var definitions for the full mechanism.
#
# Usage: .claude/skills/run-penumbra-ui-backend/smoke.sh [output-dir]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="${1:-/tmp/penumbra-ui-backend-demo-shots}"
mkdir -p "$OUT_DIR"

if [ ! -x build/demo/penumbra_ui_backend_demo ]; then
    echo "Demo binary not found -- building..."
    git submodule update --init --recursive
    cmake -S . -B build
    cmake --build build --target penumbra_ui_backend_demo
fi

if [ -z "${DISPLAY:-}" ]; then
    echo "DISPLAY is unset. This driver has only been verified against a real X/Wayland" >&2
    echo "display (DISPLAY=:0 in the environment it was written in) -- no Xvfb fallback" >&2
    echo "has been tested. Set DISPLAY and re-run, or see SKILL.md's Gotchas section." >&2
    exit 1
fi

echo "Running demo (DISPLAY=$DISPLAY)..."
DEMO_SCREENSHOT_BEFORE="$OUT_DIR/before.bmp" \
DEMO_SCREENSHOT_AFTER="$OUT_DIR/after.bmp" \
DEMO_AUTO_EXIT=1 \
    timeout 10 ./build/demo/penumbra_ui_backend_demo

for name in before after; do
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -y -loglevel error -i "$OUT_DIR/$name.bmp" "$OUT_DIR/$name.png"
    fi
done

echo ""
echo "Screenshots written to: $OUT_DIR"
ls -la "$OUT_DIR"
