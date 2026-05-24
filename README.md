# CrossCover

CrossCover is custom open-source firmware for the Xteink X4 e-reader, based on CrossPoint Reader / CrossInk and extended with Hardcover integration, reader quality-of-life features, custom themes, and e-ink focused typography.

The project targets the ESP32-C3 based Xteink X4. The device has limited RAM and no PSRAM, so the firmware is intentionally conservative: book data is cached on the SD card, network work is explicit, and sync features avoid page-by-page writes.

## Features

- EPUB, TXT, and XTC reading support.
- E-ink optimized home screens and themes, including Minimal, Lyra, RoundedRaff, and Lyra Carousel.
- Recent books, bookmarks, reading stats, and finished-book tracking.
- In-reader settings for font, size, spacing, margins, alignment, orientation, image rendering, Bionic Reading, Guide Dots, and more.
- Custom SD-card fonts and multiple build variants for size-constrained firmware images.
- WiFi file transfer and OPDS catalog support.
- KOReader Sync support.
- Hardcover integration:
  - Import a Hardcover API key from the SD card.
  - Authenticate from Settings.
  - Open a Hardcover library view from Home.
  - Link a local EPUB to a Hardcover book manually by Book ID or ISBN.
  - Find a matching Hardcover book automatically from EPUB title/author.
  - Mark a linked book as currently reading or read.
  - Upload reading progress.
  - Rate a linked book.
  - Optional auto-sync on reader close with a conservative progress threshold.

## Hardcover Setup

1. Create a Hardcover API key from your Hardcover account.
2. On the SD card, create this file:

   ```text
   /.crosspoint/hardcover_token.txt
   ```

3. Put only the token in that file. `Bearer ...` is accepted, but the plain token is preferred.
4. Insert the SD card and boot the device.
5. Open:

   ```text
   Settings > Hardcover > Import API Key
   ```

6. Then choose:

   ```text
   Authenticate
   ```

If authentication fails, open the serial monitor and look for `HDC` log lines.

## Using Hardcover In A Book

Open an EPUB, then open the reader menu and choose `Hardcover`.

Available actions:

- `Link Book ID` / `Hardcover Book ID or ISBN`: manually link the current EPUB.
- `Find Automatically`: search Hardcover using the EPUB title and author.
- `Auto-sync on Close`: toggle automatic progress sync when leaving the reader.
- `Mark Currently Reading`: set the Hardcover status.
- `Update Progress`: upload the current local reading progress.
- `Mark Read`: mark the book as read and upload 100% progress.
- `Rate`: send a 1-5 star rating.

Automatic linking uses the first Hardcover search match. For translated editions or books with ambiguous titles, manual Book ID or ISBN linking is safer.

## Build

This repo uses PlatformIO. On this Windows setup, use Python 3.11:

```powershell
py -3.11 -m platformio run -e tiny
```

The firmware image is written under:

```text
.pio/build/tiny/
```

To monitor serial logs:

```powershell
py -3.11 -m platformio device monitor
```

Useful log tag for Hardcover issues:

```text
HDC
```

## Flashing

Use the generated firmware binary from `.pio/build/tiny/` with the CrossPoint web flasher, or flash manually with `esptool`.

The project still uses the upstream ESP32-C3 partition and boot flow. If you are unsure, use the web flasher and choose a custom `.bin` file.

## Images

The boot logo is compiled into `src/images/Logo120.h` from `src/images/crosscover.png`.

Regenerate it with:

```powershell
py -3.11 scripts\convert_logo.py src\images\crosscover.png
```

That script writes `src/images/Logo120.h` directly.

## Credits

CrossCover is based on the CrossPoint Reader / CrossInk firmware work and keeps the same embedded-reader foundation while adding Hardcover-focused workflows.

The Hardcover behavior was informed by the Hardcover API documentation, the KOReader Hardcover plugin, and NickelHardcover.

AI assistance was used during development to help inspect the codebase, design the Hardcover integration, debug GraphQL/API issues, and prepare documentation.
