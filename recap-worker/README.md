# CrossCover Story So Far Worker

Public Gemini-backed recap service for CrossCover firmware, introduced in `1.6.1-crosscover.3`. It is separate from the LibGen relay in `../worker/`.

The firmware includes the service URL and TLS root certificate; end users do not need personal tokens or manual SD setup. The Worker requires a server-side `GEMINI_API_KEY` secret. Uploaded data is limited to original compressed text members and edition metadata; the server independently excludes the entire current chapter and later chapters before calling Gemini 3.5 Flash-Lite.

```text
npm ci
npm test
npx wrangler deploy
npx wrangler secret put GEMINI_API_KEY
```

Keep API keys out of source control and firmware. Access is public, provider quotas are shared, and the automatic device ID is not an authentication credential. The member cache and incomplete transfers are bounded, and overlapping plans reserve capacity. Legacy protocol 2 is retained for older firmware; current firmware uses protocol 3.

See [the complete guide](../docs/story-so-far.md) for endpoints, limits, privacy, cache retention, deployment and verification.
