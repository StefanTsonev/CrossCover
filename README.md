# CrossCover

CrossCover is a CrossInk-based firmware fork for the Xteink X4 e-reader. This fork adds Hardcover integration and related reader/library workflows; the underlying reader, file-browser, WiFi, sync, and theme features come from CrossInk.

> **This firmware is strictly for the Xteink X4. Do not flash it on an Xteink X3 or any other device.**

## ⚠️ DO NOT FLASH THIS FIRMWARE TO A USB LOCKED DEVICE ⚠️

## CrossCover Features

- Hardcover integration:
  - Import a Hardcover API key from the SD card.
  - Authenticate from Settings.
  - Open a lightweight Hardcover library view from Home.
  - Link a local EPUB to a Hardcover book manually by Book ID or ISBN.
  - Find a matching Hardcover book automatically from EPUB title/author.
  - Mark a linked book as currently reading or read.
  - Queue reading-progress updates, then send them from the Hardcover Library.
  - Rate a linked book.

## Hardcover Design

The Hardcover integration is intentionally lightweight:

- No background sync task.
- No always-on WiFi requirement.
- No page-by-page API calls.
- Books open and read fully offline.
- Hardcover requests happen only when a user opens a Hardcover action, authenticates, or manually processes queued updates from the Hardcover Library.
- Local storage is limited to the API token, per-book Hardcover links, queued updates, last synced progress, and normal reading stats on the SD card.

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

If authentication fails, open the serial monitor and look for `HDC` log lines. After making changes from a book, open `Hardcover Library` and process the pending updates.

## Using Hardcover In A Book

Open an EPUB, then open the reader menu and choose `Hardcover`.

Available actions:

- `Link Book ID` / `Hardcover Book ID or ISBN`: manually link the current EPUB.
- `Find Automatically`: search Hardcover using the EPUB title and author.
- `Mark Currently Reading`: set the Hardcover status.
- `Update Progress`: queue the current local reading progress.
- `Mark Read`: mark the book as read and queue 100% progress.
- `Rate`: queue a 1-5 star rating.

Open `Hardcover Library` after leaving the reader to send queued actions. Automatic linking searches Hardcover from the EPUB title and author, then lets you choose from the returned matches. Manual Book ID or ISBN linking is safer when accuracy matters.

## Build

This repo uses PlatformIO. On this Windows setup, use Python 3.11:

```powershell
py -3.11 -m platformio run -e default
```

For the small release variant, use `-e tiny`. The firmware image is written under:

```text
.pio/build/default/
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

This project is for the Xteink X4 only. If you are unsure, use the web flasher and choose a custom `.bin` file after reviewing the unlocker warning above.

## Credits

CrossCover is based on the CrossPoint Reader / CrossInk firmware work and keeps the same embedded-reader foundation while adding Hardcover-focused workflows.

The Hardcover behavior was informed by the Hardcover API documentation, the KOReader Hardcover plugin, and NickelHardcover.

AI assistance was used during development to help inspect the codebase, design the Hardcover integration, debug GraphQL/API issues, and prepare documentation.

## Support and Feedback

CrossCover is being developed in the open. If you find a bug or have an idea for a future feature or customization option, open an issue in this repository.

If you would like to support development, you can do so through Ko-fi:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/E3M5210X5J)
