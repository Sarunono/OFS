#!/usr/bin/env bash
# Launch OpenFunscripter as a native Wayland client.
# SDL2 still prefers the x11 driver by default, which puts the window on XWayland.
set -e
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
cd "$(dirname "$(readlink -f "$0")")/bin"
exec ./OpenFunscripter "$@"
