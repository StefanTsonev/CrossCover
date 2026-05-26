# CrossCover

CrossCover is custom open-source firmware for the Xteink X4 e-reader, based on CrossPoint Reader / CrossInk and extended with Hardcover integration, reader quality-of-life features, custom themes, and e-ink focused typography.

The project targets the ESP32-C3 based Xteink X4. The device has limited RAM and no PSRAM, so the firmware is intentionally conservative: book data is cached on the SD card, network work is explicit, and sync features avoid page-by-page writes.

This is a personal working fork, not a clean upstream CrossPoint pull-request branch. The code is available for testing and cherry-picking, but it includes CrossCover-specific branding, themes, and reader workflow changes mixed with the Hardcover and ETA work.

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
  - Open a lightweight Hardcover library view from Home.
  - Link a local EPUB to a Hardcover book manually by Book ID or ISBN.
  - Find a matching Hardcover book automatically from EPUB title/author.
  - Mark a linked book as currently reading or read.
  - Upload reading progress.
  - Rate a linked book.
  - Optional auto-sync on reader close with a configurable progress threshold.
- Reader status bar ETA modes for remaining time in the chapter, book, both, or hidden.
- Optional reading session summary after leaving an EPUB.

## Hardcover Design

The Hardcover integration is intentionally lightweight:

- No background sync task.
- No always-on WiFi requirement.
- No page-by-page API calls.
- Books open and read fully offline.
- Hardcover requests happen only when a user opens a Hardcover action, or once on reader close if auto-sync is enabled and the progress changed enough.
- Local storage is limited to the API token, per-book Hardcover links, last synced progress, and normal reading stats on the SD card.

The Home `Hardcover Library` view is a convenience/status screen, not a full library browser. It fetches a small limited list so large Hardcover libraries do not have to be loaded into RAM. The most reliable flow is linking and syncing the currently open EPUB.

## Hardcover Setup

1. Create a Hardcover API key from your Hardcover account.
2. On the SD card, create this file:

   ```text
   /.crosspoint/hardcover_token.txt
   ```

3. Put only the token in that file.
4. Insert the SD card and boot the device.
5. Open:

   ```text
   Settings > System > Hardcover > Import API Key
   ```

6. Then choose:

   ```text
   Authenticate
   ```

If authentication fails, open the serial monitor and look for `HDC` log lines.

Useful Hardcover settings:

- `Auto-sync Threshold`: cycles through 1%, 5%, 10%, and 15%.
- Per-book `Auto-sync on Close`: enabled from the reader's Hardcover menu after the book is linked.

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

Automatic linking searches Hardcover from the EPUB title and author, then lets you choose from the returned matches. It is a convenience shortcut and may not always find the expected book or edition, especially for translated editions, box sets, alternate titles, or books with ambiguous metadata. Manual Book ID or ISBN linking is safer when accuracy matters.

## Reader Settings

Useful reader/system settings added in this fork:

- `Reader > Customise Status Bar > Remaining Time`: choose chapter, book, both, or hidden ETA display.
- `System > Reading Session`: turn the EPUB exit summary on or off.

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

Use the generated firmware binary with the CrossPoint web flasher, or flash manually with `esptool`.

The project still uses the upstream ESP32-C3 partition and boot flow. If you are unsure, use the web flasher and choose a custom `.bin` file.

## Credits

CrossCover is based on the CrossPoint Reader / CrossInk firmware work and keeps the same embedded-reader foundation while adding Hardcover-focused workflows.

The Hardcover behavior was informed by the Hardcover API documentation, the KOReader Hardcover plugin, and NickelHardcover.

AI assistance was used during development to help inspect the codebase, design the Hardcover integration, debug GraphQL/API issues, and prepare documentation.
