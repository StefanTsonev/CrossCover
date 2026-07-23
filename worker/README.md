# CrossCover Anna’s Archive Worker

This Worker keeps Anna's Archive scraping and mirror resolution off the ESP32.
The device only calls `/search?q=...` and streams `/download?md5=...` to SD.

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
wrangler deploy --var UPSTREAM_ORIGIN:https://annas-archive.example
```
