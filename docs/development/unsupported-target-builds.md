---
title: Building Unofficial Device Targets
parent: Development
nav_order: 5
---

# Building Unofficial Device Targets

Official CrossCover releases provide only the `default` X3/X4 firmware. The
maintainer does not own a Sticky, X4 Pro, or X4 Classic and therefore cannot
validate their display, touch, storage, USB, sleep, or power behavior.

The inherited CrossInk build environments remain in the source tree for
advanced users who want to compile them locally:

```sh
git submodule update --init --recursive
pio run -e sticky
pio run -e x4-pro
pio run -e x4-classic
```

In PowerShell, if `pio` is not in `PATH`, use the PlatformIO virtual
environment directly. For example:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e x4-pro
```

A successful compilation proves only that the source builds. It does not prove
that the firmware is safe or fully functional on that device. These images are
unofficial, untested, and used at your own risk. Include the exact environment,
device model, firmware size, and serial log when reporting a problem.
