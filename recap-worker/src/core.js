export const VERSION = 'epub-prefix-v2';
export const LIMITS = Object.freeze({body: 16384, chapters: 128});
const encoder = new TextEncoder();
export const bytes = value => encoder.encode(value).length;
export function reply(data, status = 200, extra = {}) {
  return new Response(JSON.stringify(data), {status, headers: {'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store', ...extra}});
}
export async function readBounded(response, maximum = LIMITS.body) {
  if (Number(response.headers.get('content-length')) > maximum) throw new Error('too_large');
  if (!response.body) return '';
  const reader = response.body.getReader();
  const decoder = new TextDecoder('utf-8', {fatal: true});
  let size = 0, text = '';
  try {
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > maximum) throw new Error('too_large');
      text += decoder.decode(value, {stream: true});
    }
    return text + decoder.decode();
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
}
const bounded = (s, max) => typeof s === 'string' && s.trim().length > 0 && bytes(s) <= max;
export function validate(value) {
  if (![2, 3].includes(value?.protocol) || !(value.protocol === 3 ? /^[a-f0-9]{64}$/ : /^[a-f0-9]{16}$/).test(value.bookKey || '') ||
      !Number.isInteger(value.cutoff?.spine) || value.cutoff.spine <= 0 || value.cutoff.spine > 4096 ||
      !bounded(value.cutoff?.href, 1024) || !bounded(value.throughTitle, 160) ||
      !Number.isInteger(value.summarizeThroughChapter) || value.summarizeThroughChapter < 0 || value.summarizeThroughChapter >= 128)
    throw new Error('invalid_boundary');
  return {protocol: value.protocol, bookKey: value.bookKey, cutoff: {spine: value.cutoff.spine, href: value.cutoff.href},
    throughTitle: value.throughTitle, summarizeThroughChapter: value.summarizeThroughChapter};
}
export async function digest(value) {
  return [...new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(value)))].map(b => b.toString(16).padStart(2, '0')).join('');
}
export async function keyFor(value, model) { return digest(JSON.stringify([VERSION, model, value])); }
