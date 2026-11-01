---
title: Troubleshooting
nav_order: 14
---

# Troubleshooting

This document shows common issues and possible solutions while using the device features.

- [Troubleshooting](#troubleshooting)
  - [Cannot See the Device on the Network](#cannot-see-the-device-on-the-network)
  - [Connection Drops or Times Out](#connection-drops-or-times-out)
  - [Upload Fails](#upload-fails)
  - [Saved Password Not Working](#saved-password-not-working)

### Cannot See the Device on the Network

**Problem:** Browser shows "Cannot connect" or "Site can't be reached"

**Solutions:**

1. Verify both devices are on the correct network (must be a 2.4ghz network)
   - Check your computer/phone Wi-Fi settings
   - In **Join Network** mode, your computer/phone and CrossCover must be on the same Wi-Fi network
   - In **Create Hotspot** mode, your computer/phone must be connected to the `CrossPoint-Reader` hotspot
2. Double-check the IP address
   - Make sure you typed it correctly
   - Include `http://` at the beginning
   - Try the displayed IP address if `http://crosspoint.local/` does not resolve
3. Try disabling VPN if you're using one
4. Some networks have "client isolation" enabled - use Create Hotspot mode or check with your network administrator

### Connection Drops or Times Out

**Problem:** Wi-Fi connection is unstable

**Solutions:**

1. Move closer to the Wi-Fi router, or use Create Hotspot mode for a direct connection
2. Check signal strength on the device (should be at least `||` or better)
3. Avoid interference from other devices
4. Try a different Wi-Fi network if available (must be a 2.4ghz network)

### Upload Fails

**Problem:** File upload doesn't complete or shows an error

**Solutions:**

1. Check that the SD card has enough free space
2. Check that the filename is valid for the SD card filesystem
3. Try uploading a smaller file first to test
4. Refresh the browser page and try again
5. If WebSocket upload fails repeatedly, refresh the page and retry with the HTTP fallback path

### Saved Password Not Working

**Problem:** Device fails to connect with saved credentials. This often happens when swapping between firmwares or devices due to the password hashing mechanism.

**Solutions:**

1. When connection fails, you'll be prompted to "Forget Network"
2. Select **Yes** to remove the saved password
3. Reconnect and enter the password again
4. Choose to save the new password

## Story So Far

- **No completed chapters / unsupported boundaries:** finish at least one supported chapter. The whole current chapter is excluded; unsupported or ambiguous divisions are not guessed.
- **Minute limit:** wait for the displayed provider-supplied interval, if present, before another attempt. If no interval is given, try again later.
- **Daily limit:** try after the provider resets its quota. Saved recaps still open offline.
- **Busy / interrupted upload:** use Retry after a short wait. Completed compressed members and durable checkpoints are reused when retained by the bounded cache.
- **Missing setup:** normal firmware needs no manual SD setup. If custom `recap.json` or `recap-ca.pem` overrides are present, check them or restore the built-in service by removing only those overrides.
- **Unexpected old recap after replacing an EPUB:** local cache identity is metadata/path based. Remove only the affected `recap-<hash>.json` result cache; do not delete the whole `.crosspoint` folder, which also holds reading progress and settings.

See [Story So Far](./story-so-far.md) for the data sent to the service, limits and serial diagnostics.
