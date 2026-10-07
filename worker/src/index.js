const MAX_RESULTS = 8;
const MAX_QUERY_LENGTH = 96;
const MAX_SEARCH_RESPONSE_BYTES = 15 * 1024;
const MAX_SEARCH_HTML_BYTES = 256 * 1024;
const utf8Encoder = new TextEncoder();

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });
}

function upstreamUrl(env, path) {
  return new URL(path, env.UPSTREAM_ORIGIN.endsWith("/") ? env.UPSTREAM_ORIGIN : `${env.UPSTREAM_ORIGIN}/`);
}

function coverProxyUrl(origin, coverUrl) {
  return `${origin}/cover?url=${encodeURIComponent(coverUrl)}`;
}

function decodeHtml(value) {
  return value.replace(/&amp;/g, "&").replace(/&quot;/g, '"').replace(/&#39;/g, "'")
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/\s+/g, " ").trim();
}

function limitUtf8(value, maxBytes) {
  let result = "";
  let bytes = 0;
  for (const character of value) {
    const characterBytes = utf8Encoder.encode(character).length;
    if (bytes + characterBytes > maxBytes) break;
    result += character;
    bytes += characterBytes;
  }
  return result;
}

async function readTextLimited(response, maxBytes) {
  if (!response.body) return "";
  const reader = response.body.getReader();
  const chunks = [];
  let totalBytes = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    totalBytes += value.byteLength;
    if (totalBytes > maxBytes) {
      await reader.cancel();
      throw new Error("upstream response exceeded size limit");
    }
    chunks.push(value);
  }
  const bytes = new Uint8Array(totalBytes);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return new TextDecoder().decode(bytes);
}

function scrapeLibGenResults(html, origin) {
  const results = [];
  const rows = html.matchAll(/<tr\b[^>]*>([\s\S]*?)<\/tr>/gi);
  for (const rowMatch of rows) {
    if (results.length >= MAX_RESULTS) break;
    const row = rowMatch[1];
    // Some LibGen rows append downloadname (or other query parameters) after
    // the MD5. Match the MD5 prefix while keeping the entire href quoted.
    const md5Match = row.match(/href=["']\/ads\.php\?md5=([a-f0-9]{32})(?:&[^"']*)?["']/i);
    if (!md5Match) continue;
    const cells = [...row.matchAll(/<td\b[^>]*>([\s\S]*?)<\/td>/gi)].map((match) => match[1]);
    if (cells.length < 9) continue;
    const coverMatch = cells[0].match(/<img\b[^>]*src=["']([^"']+)["']/i);
    const titleMatch = cells[1].match(/href=["']edition\.php\?id=[^"']+["'][^>]*>([\s\S]*?)<\/a>/i);
    const boldTitle = cells[1].match(/<b>([\s\S]*?)<\/b>/i);
    const plainText = (html) => decodeHtml(html.replace(/<[^>]+>/g, " "))
      .replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim();
    const title = limitUtf8(plainText(boldTitle ? boldTitle[1] : titleMatch ? titleMatch[1] : ""), 256);
    const author = limitUtf8(plainText(cells[2]), 128);
    const year = limitUtf8(plainText(cells[4]), 24);
    const language = limitUtf8(plainText(cells[5]), 48);
    const size = limitUtf8(plainText(cells[7]), 48);
    const format = decodeHtml(cells[8].replace(/<[^>]+>/g, " ")).toLowerCase();
    if (!title || format !== "epub") continue;
    let cover = "";
    if (coverMatch) {
      try {
        const coverUrl = new URL(coverMatch[1].replace(/&amp;/g, "&"), "https://libgen.li");
        if (coverUrl.protocol === "https:" && coverUrl.hostname === "libgen.li" &&
            (coverUrl.pathname.startsWith("/covers/") || coverUrl.pathname.startsWith("/fictioncovers/"))) {
          cover = limitUtf8(coverUrl.toString(), 200);
        }
      } catch (_) {
        // A malformed cover should not discard an otherwise valid book result.
      }
    }
    results.push({ title, author, year, language, format, size,
      cover: cover ? coverProxyUrl(origin, cover) : "",
      downloads: "", md5: md5Match[1],
      download: `${origin}/download?md5=${md5Match[1]}` });
  }
  return results;
}

async function fetchLibGen(env, path) {
  return fetch(upstreamUrl(env, path), {
    headers: { "user-agent": "CrossCover-LibGen/1.0", accept: "text/html" },
    redirect: "follow",
  });
}

async function resolveLibGenMirror(env, md5) {
  const resolver = await fetchLibGen(env, `/ads.php?md5=${md5}`);
  if (!resolver.ok) return null;
  const html = await readTextLimited(resolver, 128 * 1024);
  const match = html.match(/href=["']([^"']*get\.php[^"']*)["']/i);
  return match ? new URL(match[1].replace(/&amp;/g, "&"), resolver.url).toString() : null;
}

async function handle(request, env) {
  const url = new URL(request.url);
  if (url.pathname === "/search") {
    const query = (url.searchParams.get("q") || "").trim().slice(0, MAX_QUERY_LENGTH);
    if (!query) return json({ error: "missing query" }, 400);
    const upstream = await fetchLibGen(env,
      `/index.php?req=${encodeURIComponent(query)}&columns%5B%5D=t&columns%5B%5D=a&columns%5B%5D=s&columns%5B%5D=y&columns%5B%5D=p&columns%5B%5D=i&objects%5B%5D=f&objects%5B%5D=e&objects%5B%5D=s&objects%5B%5D=a&objects%5B%5D=p&objects%5B%5D=w&topics%5B%5D=l&topics%5B%5D=c&topics%5B%5D=f&topics%5B%5D=a&topics%5B%5D=m&topics%5B%5D=r&topics%5B%5D=s&res=25&covers=on&filesuns=all`);
    if (!upstream.ok) return json({ error: `upstream HTTP ${upstream.status}` }, 502);
    const results = scrapeLibGenResults(await readTextLimited(upstream, MAX_SEARCH_HTML_BYTES), url.origin);
    const payload = { results };
    let body = JSON.stringify(payload);
    while (utf8Encoder.encode(body).byteLength > MAX_SEARCH_RESPONSE_BYTES && results.length > 0) {
      results.pop();
      body = JSON.stringify(payload);
    }
    return new Response(body, {
      headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
    });
  }
  if (url.pathname === "/download") {
    const md5 = url.searchParams.get("md5") || "";
    if (!/^[a-f0-9]{32}$/i.test(md5)) return json({ error: "invalid md5" }, 400);
    const mirror = await resolveLibGenMirror(env, md5);
    if (!mirror) return json({ error: "no mirror" }, 404);
    // The file CDN rejects Cloudflare-to-CDN proxying. Let the firmware follow
    // the HTTPS redirect itself; HttpDownloader already supports this chain.
    return Response.redirect(mirror, 307);
  }
  if (url.pathname === "/cover") {
    const coverUrl = url.searchParams.get("url") || "";
    let target;
    try {
      target = new URL(coverUrl);
    } catch (_) {
      return json({ error: "invalid cover URL" }, 400);
    }
    if (target.protocol !== "https:" || target.hostname !== "libgen.li" ||
        !(target.pathname.startsWith("/covers/") || target.pathname.startsWith("/fictioncovers/"))) {
      return json({ error: "unsupported cover URL" }, 400);
    }
    const cover = await fetch(target, {
      headers: {
        accept: "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8",
        referer: "https://libgen.li/",
        "user-agent": "Mozilla/5.0 (CrossCover; LibGen cover proxy)",
      },
      cf: {
        cacheEverything: true,
        cacheTtl: 604800,
        // scale-down can pass small originals through unchanged, including
        // chroma layouts JPEGDEC cannot decode. Resize and desaturate for e-ink
        // so these thumbnails go through the image encoder as well.
        image: { width: 246, height: 360, fit: "contain", saturation: 0, format: "jpeg", quality: 75 },
      },
    });
    if (!cover.ok || !cover.body) return json({ error: `cover HTTP ${cover.status}` }, 502);
    const headers = new Headers(cover.headers);
    headers.set("content-type", "image/jpeg");
    headers.set("cache-control", "public, max-age=604800");
    headers.delete("content-length");
    return new Response(cover.body, { status: cover.status, headers });
  }
  return json({ service: "crosscover-libgen", endpoints: ["/search?q=...", "/download?md5=..."] });
}

export default {
  async fetch(request, env) {
    try {
      return await handle(request, env);
    } catch (error) {
      console.error(JSON.stringify({
        message: "LibGen request failed",
        path: new URL(request.url).pathname,
        error: error instanceof Error ? error.message : String(error),
      }));
      return json({ error: "upstream request failed" }, 502);
    }
  },
};
