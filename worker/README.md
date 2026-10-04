# CrossCover LibGen Worker

This Worker keeps LibGen scraping and download resolution off the ESP32.
The device calls `/search?q=...`, streams `/download?md5=...` to SD, and uses
`/cover?url=...` for LibGen thumbnails. The cover endpoint adds the headers
LibGen requires and caches the image at the edge.

Search checks the first 25 LibGen catalog entries and returns up to 8 EPUB matches.

Deploy with Wrangler:

```sh
npm install -g wrangler
wrangler login
wrangler deploy
```

After deployment, set the returned `https://...workers.dev` URL in a local
PlatformIO override (do not commit it):

```ini
[env:default]
build_flags =
  ${base.build_flags}
  -DSHADOW_LIBRARY_BASE_URL=\"https://YOUR_WORKER.workers.dev\"
```

The upstream domain is configured in `wrangler.toml`. It can be changed with:

```sh
wrangler deploy --var UPSTREAM_ORIGIN:https://libgen.li
```
