---
title: CrossCover Customization Manifest
parent: Contributing
nav_order: 5
---

# CrossCover Customization Manifest

This is the source of truth for CrossCover-specific behavior. When importing a
new CrossInk release, compare the post-merge tree with this manifest before
resolving UI, SDK, networking, or release conflicts.

## Upstream baseline

The integration branch must contain the complete upstream release first. Do not
copy files from an older CrossCover release over the new upstream tree.

Verify these areas against the upstream tag:

- SDK and submodules (`.gitmodules`, `freeink-sdk`, `open-x4-sdk` policy)
- `platformio.ini`, partitions, build scripts, and firmware size checks
- `src/util/Dictionary*`, dictionary reader activities, and dictionary tests
- generated i18n, web, and font assets
- reader, settings, home, network, and activity-manager APIs

Remove obsolete CrossCover implementations when upstream replaces them. In
particular, do not retain the pre-v1.5 dictionary implementation alongside the
v1.5 dictionary pipeline.

Known obsolete dictionary files that must not return during an integration:

- `lib/Dictionary/DictionaryRegistry.*`
- `lib/Dictionary/StarDictReader.*`
- `src/activities/reader/DictionaryActivity.*`

The active v1.5 dictionary implementation is under `src/util/Dictionary*`,
`src/util/DictionaryLookup*`, `src/util/DictionaryRegistry.*`, and the v1.5
reader dictionary activities. Search for references before deleting or keeping
any dictionary file; PlatformIO may compile unreferenced files under `lib/`.

## CrossCover features that must survive

### Hardcover

Purpose: authenticated library access, linking, progress, status, and ratings.

Review these files and flows after every upstream merge:

- `lib/Hardcover/`
- `src/activities/home/HardcoverLibraryActivity.*`
- `src/activities/settings/HardcoverSettingsActivity.*`
- `src/activities/reader/HardcoverBookActivity.*`
- `src/activities/home/HomeActivity.*`
- `src/activities/reader/EpubReaderActivity.*`
- `src/activities/reader/EpubReaderMenuActivity.*`
- `src/SettingsList.h` and `src/activities/settings/SettingsActivity.*`
- `lib/I18n/translations/*.yaml` Hardcover strings

Required behavior:

- Hardcover is available from `Home -> CrossCover`.
- Hardcover setup is available from `Settings -> System`.
- The reader menu exposes the Hardcover actions.
- TLS requests work on X3/X4 without requiring an RTC to be present.
- API credentials remain stored/importable through the existing CrossCover path.
- Progress and other updates remain queued where the device is offline.

### Anna's Archive

Purpose: low-memory search and download through the CrossCover Cloudflare
Worker relay.

Review these files and flows after every upstream merge:

- `src/network/ShadowLibraryClient.*`
- `src/network/ShadowLibrarySettings.*`
- `src/activities/browser/ShadowLibraryActivity.*`
- `src/activities/home/HomeActivity.*`
- `src/SettingsList.h` and `src/activities/settings/SettingsActivity.*`
- `worker/` and its deployed Worker URL/configuration
- `lib/I18n/translations/*.yaml` Anna's Archive strings

Required behavior:

- Anna's Archive is available from `Home -> CrossCover`.
- Search results display title, author, format, size, and downloads.
- Downloads use the configured folder and do not require an in-firmware mirror parser.
- The Worker URL is configured explicitly and is not replaced by an upstream URL.
- Search and download remain bounded and streaming-friendly for the ESP32-C3.
- Search responses use a bounded `nothrow` buffer and heap guard; never restore
  an unbounded `std::string` response accumulator for the Worker JSON.

Transport rule for the v1.5 FreeInk network stack:

- Worker search and `/download` transfers must set
  `HttpDownloader::Transport::WOLFSSL` explicitly. The default mbedTLS path can
  consume almost all remaining heap after a Wi-Fi scan and cause a connection
  timeout.
- Do not assume that a working HTTPS search request proves that a redirected or
  streamed file download can use the same transport.
- When reviewing upstream `HttpDownloader` changes, compare every
  `fetchUrl`, `streamUrl`, and `downloadToFile` call site. Check both the
  transport and buffer-size options, not only the downloader implementation.

### Branding

User-facing CrossCover branding must be reviewed in these locations:

- `src/images/Logo120.h` and `src/images/crosscover.png`
- `lib/I18n/translations/*` value of `STR_CROSSINK`
- `src/activities/boot_sleep/BootActivity.cpp`
- `src/activities/boot_sleep/SleepActivity.cpp`
- `src/CrossPointSettings.cpp` default device names
- `src/activities/settings/SettingsActivity.cpp` version footer
- `lib/hal/HalSystem.cpp` diagnostic labels
- README, CHANGELOG, release notes, and firmware version metadata

Keep technical identifiers such as `EpubRenderMode::CrossInkDefault`, upstream
SDK names, and internal compatibility paths unless changing them would alter
behavior. Branding review concerns user-visible text, not every source symbol
that contains the upstream project name.

## Intentional CrossCover differences from upstream

These are product decisions, not merge conflicts to be removed:

- Hardcover integration and its UI/actions.
- Anna's Archive relay integration and download-folder setting.
- CrossCover branding and release metadata.
- CrossCover-specific menu grouping and the hidden `selected` marker in the
  provider picker.
- CrossCover TLS compatibility adapter used by Hardcover.

Everything else should match the upstream release unless a documented hardware
compatibility fix is added to this manifest.

## Integration checklist

For each new upstream release:

1. Create `integrate/crossink-vX.Y.Z` from current `main`.
2. Merge the upstream tag with the upstream merge commit preserved.
3. Review `.gitmodules`, SDK paths, `platformio.ini`, and partition limits.
4. Compare every section above and restore only the listed CrossCover deltas.
5. Search for stale files replaced by upstream, especially old dictionary code.
6. Regenerate i18n/web outputs through their scripts; never edit generated files.
7. Run clang-format, simulator build, static analysis, and default firmware build.
8. Flash X3 and X4 as applicable and test the exact Hardcover, Anna's Archive,
   dictionary, boot branding, and Settings paths.
9. Add a changelog section naming both the upstream base and CrossCover changes.
10. Merge to `main`, tag the CrossCover release, publish artifacts, and delete
    the temporary integration branch.

## Required release evidence

Record the following in the pull request or release checklist:

- upstream tag and CrossCover commit/tag;
- files intentionally different from upstream;
- device(s) tested and firmware artifact name;
- default build size and remaining OTA space;
- exact UI paths tested;
- serial logs for network and dictionary tests;
- Worker URL/version used for Anna's Archive;
- confirmation that no obsolete duplicate implementation remains.
