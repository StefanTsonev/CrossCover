---
title: Simulator
parent: Development
nav_order: 4
---

# Development Device Simulator

CrossCover can run in the inherited [CrossInk simulator](https://github.com/uxjulia/crossink-simulator), which renders the e-ink display in an SDL2 window. Use it for quick sanity checks without flashing firmware every time.

## Native Windows

From PowerShell in the firmware directory, run:

```powershell
.\simulator.cmd
```

The first run downloads a portable 64-bit C++ compiler and SDL2 into
`.cache/windows-simulator`, prepares a separate pinned simulator checkout with
the Windows compatibility patch, and builds the desktop application. It requires
Git and the Python environment installed by PlatformIO. No Ubuntu, WSL, system
PATH changes, or firmware upload is involved.

Later runs open the existing application immediately. After changing firmware
code, close the simulator window first (Windows locks running executables), then
rebuild and open it with:

```powershell
.\simulator.cmd --build
```

Use `--build-only` to compile without opening the window, and `-j 8` to select
eight compiler jobs. Windows outputs use `.pio/build/simulator-windows`, separate
from hardware and Linux simulator builds. The launcher preserves
`platformio.local.ini` and generates its build configuration inside `.cache`.

Press **F12** to save a clean BMP screenshot in
`.cache/windows-simulator/screenshots`. Screenshots are taken by the SDL main
thread after drawing the current frame, without window borders.

The application uses `fs_/books` as its virtual SD card book folder. Device
settings and cached state persist under `fs_/.crosspoint`; they are independent
of the real reader's SD card. The simulator can access network services, so use
test credentials and explicit user actions when testing synchronization.

Use the desktop application to check layout, navigation, and reading flows.
Its host memory, Windows HTTP/TLS backend, and SDL refresh behavior do not model
the ESP32-C3's RAM limits or physical e-ink refresh. Verify those on the reader.

### Visual audit captures

The Windows simulator smoke test can save the completed SDL frame after every
named render step. Set `CROSSINK_SIMULATOR_SMOKE_TEST=1`,
`CROSSINK_SIMULATOR_SMOKE_VISUAL_AUDIT=1`, and
`CROSSPOINT_SIM_SCREENSHOT_DIR` to a capture directory when launching the native
binary. `CROSSINK_SIMULATOR_SMOKE_BOOK` selects the fixture's virtual SD path.
Run automated tests from a separate working directory containing its own `fs_`
folder so the test's settings, books, and library index do not replace the
interactive simulator session. The log records each step and its BMP filename.

## Linux And macOS

Use the existing `simulator` PlatformIO environment with SDL2 installed on the
host. Linux also requires OpenSSL development headers for MD5 support.

## Prerequisites

```sh
# macOS
brew install sdl2

# Linux (Debian/Ubuntu)
sudo apt install libsdl2-dev libssl-dev
```

## Setup

Place EPUB books in `./fs_/books/` relative to the project root. That maps to the SD-card `/books/` path on device.

## Build And Run

```sh
pio run -e simulator
.pio/build/simulator/program
```

Use the X4 Pro environment to enable its touch, frontlight, and Home-key behavior:

```sh
pio run -e x4-pro-simulator -t run_simulator
```

## Keyboard Controls

| Key | Action |
| --- | --- |
| Up / Down | Page back / forward (side buttons) |
| Left / Right | Left / right front buttons |
| Return | Confirm / Select |
| Escape | Back |
| P | Power |
| F12 | Save a BMP screenshot (Windows launcher) |
| H | X4 Pro Home key (tap to go Home; hold for 700 ms to toggle the reader menu) |

The `H` mapping is active only in `x4-pro-simulator`.

## Cache Note

On first open of an EPUB, an **Indexing...** popup appears while the section cache is built in `.crosspoint/`.

If rendering looks stale after a code change, delete `./fs_/.crosspoint/` to clear simulator caches.
