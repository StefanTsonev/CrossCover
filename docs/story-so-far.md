# Story So Far

Introduced in CrossCover `1.6.1-crosscover.3` (CrossInk `1.6.1` base).

Story So Far creates a short recap from the actual text of completed EPUB chapters. The entire current chapter, including its earlier spine files, is excluded before any model request. Gemini does not receive future chapter text or the full table of contents. Google Search is disabled.

## User flow

1. Open Story So Far in the reader menu. An existing local recap opens offline.
2. Otherwise the standard Cancel / Confirm popup describes uploading the required text.
3. Confirm saves the reading position and uses minimal network boot target 11, releasing the reader before Wi-Fi/TLS on devices without PSRAM.
4. The device shows Preparing book, Uploading book with progress, then Creating recap. Progress redraws are limited to changed percentages at five-second intervals, with immediate stage/completion updates.
5. The result shows the last completed chapter, a concise AI notice, the recap, and page navigation when needed. Back returns to the saved reader position. Temporary failures offer Retry.

## Transfer architecture (protocol 3)

The device sends `META-INF/container.xml`, the package OPF and the spine files strictly before the current chapter. It copies their **original compressed bytes**, skipping images, fonts, CSS and future document payloads. It does not inflate/recompress chapters, assemble another EPUB, or buffer a book in RAM. The full OPF contains edition metadata and spine ordering; only the extracted completed narrative, book title and language reach Gemini.

A single directory pass selects raw ZIP members and validates their local headers and bounds. Unselected assets do not require local-header seeks. SHA-256 is computed incrementally over each selected member using the same 4 KiB scratch buffer as networking. At most 256 fixed member descriptors are allocated once (about 6 KiB). The 16 KiB local handoff and 64 KiB wire-manifest bounds may impose a lower effective member count for long paths. Allocation failures and low internal heap return a recoverable error.

The Worker returns a mask of missing members. Their compressed bytes are concatenated into one logical stream and sent in at most 1 MiB requests, with no per-chapter HTTP request. Adding chapters uploads only missing members. Going back to an earlier chapter or switching books can reuse the retained member cache. A completed or resumable plan can survive a Worker restart.

Real hardware reuses one verified TLS connection for the manifest, streamed segments and final recap request, reconnecting if the server closes it. There are never two simultaneous TLS sessions. The simulator's text-only HTTP shim uses base64 on host RAM and does not measure embedded TLS throughput.

Each received member is checked against its SHA-256, CRC, declared inflated size and UTF-8 validity before publication. The Worker independently parses the container/OPF and validates the exact cutoff href and spine position before extracting text. Missing documents, ambiguous boundaries and corrupted uploads never trigger generation. No automatic fallback uploads the full EPUB.

## Storage and limits

- **SD:** the existing EPUB, small per-recap JSON caches (at most 8 KiB), a handoff and a non-secret device ID. A temporary descriptor JSON (at most 64 KiB) is removed after the transfer attempt; a power interruption can leave it until the next attempt overwrites it. No extra book or chapter payload copy is written to SD.
- **Worker member cache:** at most 64 books, 64 MiB of compressed members in total, 8 MiB and 512 member hashes per book. Oldest-used books are evicted when necessary. Book expiry is seven days and is refreshed when a new plan accesses the book. The bounded cache is shared across public devices.
- **Pending transfers:** at most eight current device plans, each with at most 8 MiB of staged bytes and a 24-hour expiry. Full completed streams release their staging chunks. A request checkpoints every 128 KiB in 16 KiB storage blocks; completed segments survive edge buffering/interruption. A displaced or expired plan is negotiated again on Retry.
- The cache budgets above cover protocol 3 data. Legacy protocol 2 archives and saved recaps have their own existing seven-day expiry.
- XML/XHTML members may inflate to at most 1 MiB each. Extracted narrative may contain at most 768 KiB; `countTokens` rejects inputs above 200,000 tokens. Context is never silently truncated.
- **Gemini 3.5 Flash-Lite (`gemini-3.5-flash-lite`)** is asked for 150-220 words, at most 250, in the book language. Output validation accepts 30-300 words and at most 4096 UTF-8 bytes.
- A cache miss makes one countTokens request and at most one generation request. Successful results are shared for seven days by the exact validated input, cutoff, model and prompt version. There is no application-imposed daily generation cap. Provider and Cloudflare account quotas remain shared limits.

Chapter mapping currently accepts at most 128 TOC entries and at most 254 spine files before the cutoff. Leading TOC links outside the reading spine are skipped. Unresolved links later in the TOC, duplicate/non-increasing chapter positions, missing titles and unsupported intra-file chapter divisions are rejected. Split chapter groups move the boundary before the whole current chapter.

Local recap identity includes the protocol, book path, layout identity, member paths and cutoff. If replacing a book in place with different text but identical metadata, clear the affected `/.crosspoint/recap-*.json` cache. Server member identity always uses actual byte hashes.

## Public service and setup

The service has no registration, personal access token or device allowlist. The firmware includes the public service URL and GlobalSign Root CA. It automatically creates `/.crosspoint/recap-device.json`; this separates upload sessions and is not an authentication credential. Anyone with a recap ID can read the saved recap; raw book/member download APIs are not exposed. Requests share the provider quota. The Worker does not log book text, API keys or upstream error bodies.

For a custom endpoint only, optionally create `/.crosspoint/recap.json`:

```json
{"url":"https://YOUR-WORKER.YOUR-SUBDOMAIN.workers.dev"}
```

An optional `/.crosspoint/recap-ca.pem` overrides the compiled trust anchor. Explicit but invalid overrides fail rather than silently changing services. TLS verification remains enabled. The Gemini API key exists only as a Worker secret. Free-tier provider processing may use supplied text to improve products; review the provider terms and process books you are entitled to use.

From `recap-worker`:

```text
npm ci
npm test
npx wrangler deploy
npx wrangler secret put GEMINI_API_KEY
```

Deploys preserve existing secrets. `RECAP_DEVICES` is no longer used. Existing firmware sending a Bearer value is supported as a non-secret identifier, without checking an allowlist.

## Protocol 3 API

All routes are public. Mutation requests carry `X-Recap-Device` (1-32 letters, digits, underscores or hyphens). Mutations run sequentially in the existing SQLite Durable Object so upload replacement cannot race generation.

- `POST /v3/plan`: bounded JSON `{entries:[{name,method,compressed,size,crc,sha}]}`. Entry 0 is the container, entry 1 the OPF, followed by completed spine files. Returns `{bookKey,missing,offset,total}`. `missing` is one `0`/`1` character per entry; `total` counts only missing compressed bytes.
- `PUT /v3/book/<bookKey>/stream/<offset>/<length>`: concatenated missing member bytes in manifest order, at most 1 MiB per request. Non-final boundaries align to 16 KiB. Returns the next durable offset. Binary is used on hardware; `X-Recap-Encoding: base64` is reserved for the desktop shim. Retry queries the plan again and seeks into the logical stream.
- `POST /v3/recaps`: `{protocol:3,bookKey,cutoff:{spine,href},summarizeThroughChapter,throughTitle}`. Validates cached members and the edition boundary, then returns a cached or newly generated recap.
- Legacy `/v2/book` uploads and `/v2/recaps` remain available for installed older firmware. Newly built firmware prepares protocol 3 handoffs; re-enter Story So Far from the reader after updating if an old network handoff was pending.
- `GET /v1/usage` reports informational usage. `GET /v1/recaps/<id>` and `/viewer#<id>` display saved recaps without a login.

## Verification

Backend tests cover strict boundaries, exclusion of future text, SHA/CRC/inflation checks, partial transfer recovery, binary/base64 equivalence, chapter-only additions, shared recap reuse across devices and restarts, multiple books, bounded pending plans, expiry and provider failures. Native tests cover streaming SHA-256 standard vectors and HTTP framing/partial writes. The embedded-session composition test verifies metadata, binary upload and an 8 KiB response on one connection using a transport fixture.

Hardware verification: on X4, select Story So Far after at least one completed chapter. Compare logs `Selected EPUB members` (original/compressed/missing/resume bytes), `Transfer body` and `Selected member upload complete` (SD read time) using the same book and Wi-Fi location. Repeat after advancing one chapter: only the new compressed member bytes should be missing. Try a second book and return to the first; it should reuse retained members. Interrupt a multi-segment upload and verify a nonzero resume offset. Confirm Back returns to the same reader page. A desktop run cannot establish device heap or Wi-Fi throughput.

Verified on 2026-10-31: 40 backend tests passed, as did native SHA-256, HTTP sender and embedded-session composition checks. The Windows simulator build passed. A native firmware run against the deployed Worker selected five members totalling 8,727 compressed bytes from a 6,480,823-byte fixture (public-domain Alice plus a deliberately unused 6 MiB asset). One Gemini generation produced a ready recap. After removing only the isolated test result cache, repeating the run made zero upload PUTs and no additional generation. Advancing the server-side member plan uploaded only the next 5,476-byte compressed chapter, reusing five members, with no additional generation. The actual reader split-chapter fixture also passed. These are correctness/transfer-volume checks; host timings do not predict X4 throughput. No ESP32 firmware build or device flash was performed by the agent.

### Quota feedback

Provider 429 responses are read with a 16 KiB bound. Structured QuotaFailure identifiers distinguish minute and daily limits; a plain 429, a retry hint alone, malformed details, or mixed unknown restrictions keep the generic quota message. Daily violations take priority over minute violations. Retry-After and RetryInfo provide an optional minimum wait; the internal 60-second fallback is not advertised as a provider reset time. The cooldown stores its category and explicit retry deadline so later users see the same cause with the remaining wait. New firmware shows minute/daily/generic messages and an explicit wait for temporary/unknown limits when supplied. Daily errors do not turn a short retry hint into a claimed reset countdown. Cached recaps remain accessible. Older firmware still recognizes the compatible budget_exhausted status.

Verified on 2026-11-01: all 52 backend tests passed, including minute/daily/unknown quota classification, retry duration/header parsing, malformed errors, shared cooldown persistence and countTokens failures without generation. Firmware translations were regenerated; no ESP32 build or artificial exhaustion of the live provider quota was performed.

### Cache reservations

Outstanding upload plans reserve unique member hashes against both the 8 MiB per-book/512-member limit and the 64 MiB global cache limit. Identical files shared by devices reserve capacity once. Incomplete plans pin the cached book files they depend on; other cached books are evicted in oldest-use order when capacity is needed. A fully received plan whose members are not yet published still reserves capacity after restart. Expired or replaced plans release their reservations. If the incoming plan cannot fit without disrupting an incomplete transfer, the Worker returns retryable busy instead of overcommitting. Publication checks the limits again to protect against legacy plans created before reservation accounting. Cache eviction updates chunks, member indexes and accounting atomically.

Verified on 2026-11-01: all 58 backend tests passed. Replaying the original local reproduction with two approximately 5 MiB versions of the same book now accepts the first plan and returns busy for the overlapping second plan. After the first completes, the second can retry; cached bytes remain below the 8 MiB limit after every segment. No live Gemini request was made for this reproduction.
