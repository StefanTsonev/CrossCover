const MAX_RESULTS = 8;
const MAX_QUERY_LENGTH = 96;

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });
}

function upstreamUrl(env, path) {
  return new URL(path, env.UPSTREAM_ORIGIN.endsWith("/") ? env.UPSTREAM_ORIGIN : `${env.UPSTREAM_ORIGIN}/`);
}

function decodeHtml(value) {
  return value.replace(/&amp;/g, "&").replace(/&quot;/g, '"').replace(/&#39;/g, "'")
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/\s+/g, " ").trim();
}

function attr(tag, name) {
  const match = tag.match(new RegExp(`${name}\\s*=\\s*["']([^"']*)["']`, "i"));
  return match ? match[1] : "";
}

function scrapeResults(html, origin) {
  const results = [];
  // Attribute order differs between mirrors, so do not require class before href.
  const links = /<a\b(?=[^>]*href=["'](\/md5\/[a-f0-9]{32})["'])(?=[^>]*class=["'][^"']*js-vim-focus[^"']*["'])[^>]*>([\s\S]*?)<\/a>/gi;
  let match;
  while ((match = links.exec(html)) !== null && results.length < MAX_RESULTS) {
    const link = match[1];
    const title = decodeHtml(match[2].replace(/<[^>]+>/g, ""));
    if (!title) continue;
    // Result cards contain metadata (including the Downloads span) after the
    // title link; keep a generous bounded window for mirrors with extra markup.
    const card = html.slice(Math.max(0, match.index - 4000), Math.min(html.length, links.lastIndex + 12000));
    const authorMatch = card.match(/<a[^>]*href=["']\/search\?q=[^"']*["'][^>]*>([\s\S]*?)<\/a>/i);
    const infoMatch = card.match(/<div[^>]*class=["'][^"']*text-gray-800[^"']*["'][^>]*>([\s\S]*?)<\/div>/i);
    const info = decodeHtml((infoMatch ? infoMatch[1] : "").replace(/<[^>]+>/g, ""));
    const format = /pdf/i.test(info) ? "pdf" : "epub";
    const sizeMatch = card.match(/\b(\d+(?:\.\d+)?\s*(?:KB|MB|GB))\b/i);
    const cardText = decodeHtml(card.replace(/<[^>]+>/g, " "));
    const downloadsValue = card.match(/<span[^>]*title=["']Downloads["'][^>]*>[\s\S]*?<span[^>]*>[^<]*<\/span>\s*([\d,.]+)\s*<\/span>/i);
    const downloadsMatch = (downloadsValue && downloadsValue[1]) ||
      cardText.match(/\b([\d,.]+)\s*(?:downloads?|downloaded)\b/i) ||
      cardText.match(/\bdownloads?\s*(?:count)?\s*[:·-]?\s*([\d,.]+)\b/i);
    const md5 = link.slice(5);
    results.push({ title, author: decodeHtml((authorMatch ? authorMatch[1] : "").replace(/<[^>]+>/g, "")), format,
      size: sizeMatch ? sizeMatch[1] : "", downloads: typeof downloadsMatch === "string" ? downloadsMatch :
        (downloadsMatch ? downloadsMatch[1] : ""), md5,
      download: `${origin}/download?md5=${md5}` });
  }
  return results;
}

async function fetchUpstream(request, env, path) {
  return fetch(upstreamUrl(env, path), {
    headers: { "user-agent": "CrossCover-AnnasArchive/1.0", accept: "text/html" },
    redirect: "follow",
  });
}

async function enrichDownloadCounts(env, results) {
  await Promise.all(results.map(async (book) => {
    try {
      const response = await fetch(upstreamUrl(env, `/dyn/md5/inline_info/${book.md5}`), {
        headers: { "user-agent": "CrossCover-AnnasArchive/1.0", accept: "text/css" },
      });
      if (!response.ok) return;
      const info = await response.json();
      if (info && info.downloads_total !== undefined) {
        book.downloads = Number(info.downloads_total).toLocaleString("en-US");
      }
    } catch (_) {
      // Search results remain useful when one metadata request fails.
    }
  }));
}

async function resolveMirror(env, md5) {
  const detail = await fetchUpstream(null, env, `/md5/${md5}`);
  if (!detail.ok) return null;
  const html = await detail.text();
  const slow = html.match(/href=["'](\/slow_download\/[^"']+)["']/i);
  if (slow) {
    const mirror = await fetchUpstream(null, env, slow[1]);
    if (mirror.ok) {
      const mirrorHtml = await mirror.text();
      const final = findDownloadLink(mirrorHtml);
      const resolved = final && await resolveCandidate(env, final);
      if (resolved) return resolved;
    }
  }
  const direct = findDownloadLink(html);
  return direct ? resolveCandidate(env, direct) : null;
}

async function resolveCandidate(env, candidate) {
  const url = candidate.startsWith("http") ? candidate : upstreamUrl(env, candidate).toString();
  // Anna exposes LibGen's ads.php page, which must be followed to get.php
  // before it can be streamed as a book.
  if (/ads\.php(?:\?|$)/i.test(url)) {
    const response = await fetch(url, { headers: { "user-agent": "CrossCover-AnnasArchive/1.0" } });
    if (!response.ok) return null;
    const html = await response.text();
    const final = html.match(/href=["']([^"']*get\.php[^"']*)["']/i);
    if (!final) return null;
    return new URL(final[1].replace(/&amp;/g, "&"), url).toString();
  }
  return url;
}

function findDownloadLink(html) {
  const links = html.matchAll(/href=["']([^"']+)["']/gi);
  let fallback = null;
  for (const match of links) {
    const href = match[1].replace(/&amp;/g, "&");
    if (!href || href.includes("annas-archive")) continue;
    if (href.startsWith("/slow_download/") || href.startsWith("/fast_download/")) continue;
    // Prefer the LibGen resolver; it leads to a real get.php file without
    // Anna's browser-verification/countdown page.
    if (/libgen\.li\/ads\.php/i.test(href)) return href;
    if (/ipfs_download|download|get\.php|\.epub(?:$|[?#])|\.pdf(?:$|[?#])/i.test(href) &&
        !/ads\.php|\/book\//i.test(href) && !fallback) fallback = href;
  }
  return fallback;
}

async function handle(request, env) {
  const url = new URL(request.url);
  if (url.pathname === "/search") {
    const query = (url.searchParams.get("q") || "").trim().slice(0, MAX_QUERY_LENGTH);
    if (!query) return json({ error: "missing query" }, 400);
    const upstream = await fetchUpstream(request, env, `/search?page=1&q=${encodeURIComponent(query)}&ext=epub`);
    if (!upstream.ok) return json({ error: `upstream HTTP ${upstream.status}` }, 502);
    const results = scrapeResults(await upstream.text(), url.origin);
    await enrichDownloadCounts(env, results);
    return json({ results });
  }
  if (url.pathname === "/download") {
    const md5 = url.searchParams.get("md5") || "";
    if (!/^[a-f0-9]{32}$/i.test(md5)) return json({ error: "invalid md5" }, 400);
    const mirror = await resolveMirror(env, md5);
    if (!mirror) return json({ error: "no mirror" }, 404);
    const response = await fetch(mirror, { headers: { "user-agent": "CrossCover-AnnasArchive/1.0" } });
    if (!response.ok) return json({ error: `download HTTP ${response.status}` }, 502);
    const contentType = response.headers.get("content-type") || "";
    if (/text\/html/i.test(contentType)) return json({ error: "mirror returned HTML instead of a book" }, 502);
    const headers = new Headers(response.headers);
    headers.set("content-disposition", `attachment; filename="${md5}.epub"`);
    headers.set("cache-control", "no-store");
    return new Response(response.body, { status: response.status, headers });
  }
  return json({ service: "crosscover-annas-archive", endpoints: ["/search?q=...", "/download?md5=..."] });
}

export default { fetch: handle };
