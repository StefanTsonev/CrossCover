import {inflateRawSync} from 'node:zlib';
import {DOMParser} from '@xmldom/xmldom';

export const MAX_EPUB = 8 * 1024 * 1024;
const MAX_XML = 1024 * 1024;
const MAX_TEXT = 768 * 1024;
const fail = () => { throw new Error('unsupported_book'); };
const decoder = new TextDecoder('utf-8', {fatal: true});
export function pathFor(base, href) {
  if (typeof href !== 'string' || /[\\\u0000?#]/u.test(href) || /^[a-z]+:/i.test(href) || href.startsWith('/')) fail();
  let decoded;
  try { decoded = decodeURIComponent(href); } catch { fail(); }
  const parts = base.split('/').slice(0, -1);
  for (const part of decoded.split('/')) {
    if (part === '..') { if (!parts.length) fail(); parts.pop(); }
    else if (part && part !== '.') parts.push(part);
  }
  if (parts.some(p => /[\\\u0000?#]/u.test(p))) fail();
  return parts.join('/');
}
export function crc32(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
// Read central-directory sizes before inflating. Images and future chapters are
// never inflated, and maxOutputLength also guards against lying ZIP headers.
export function archive(raw) {
  const data = new Uint8Array(raw);
  if (data.length < 22 || data.length > MAX_EPUB) fail();
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let end = data.length - 22;
  for (; end >= Math.max(0, data.length - 65557); end--) {
    if (view.getUint32(end, true) === 0x06054b50 && end + 22 + view.getUint16(end + 20, true) === data.length) break;
  }
  if (end < 0 || view.getUint32(end, true) !== 0x06054b50) fail();
  const count = view.getUint16(end + 10, true), start = view.getUint32(end + 16, true);
  if (!count || count > 2048 || view.getUint32(end + 4, true) !== 0 || view.getUint16(end + 8, true) !== count ||
      start + view.getUint32(end + 12, true) !== end) fail();
  const entries = new Map();
  let p = start;
  for (let i = 0; i < count; i++) {
    if (p + 46 > end || view.getUint32(p, true) !== 0x02014b50) fail();
    const flags = view.getUint16(p + 8, true), method = view.getUint16(p + 10, true);
    const compressed = view.getUint32(p + 20, true), size = view.getUint32(p + 24, true);
    const nameLength = view.getUint16(p + 28, true), offset = view.getUint32(p + 42, true);
    const next = p + 46 + nameLength + view.getUint16(p + 30, true) + view.getUint16(p + 32, true);
    if (next > end || !nameLength || offset + 30 > start || flags & 1 || ![0, 8].includes(method)) fail();
    const name = decoder.decode(data.subarray(p + 46, p + 46 + nameLength));
    if (name.startsWith('/') || name.split('/').includes('..') || /[\\\u0000]/u.test(name) || entries.has(name)) fail();
    if (view.getUint32(offset, true) !== 0x04034b50 || view.getUint16(offset + 8, true) !== method ||
        view.getUint16(offset + 6, true) !== flags) fail();
    const localNameLength = view.getUint16(offset + 26, true);
    if (offset + 30 + localNameLength > start || decoder.decode(data.subarray(offset + 30, offset + 30 + localNameLength)) !== name) fail();
    const content = offset + 30 + localNameLength + view.getUint16(offset + 28, true);
    if (content + compressed > start) fail();
    entries.set(name, {content, compressed, size, method, crc: view.getUint32(p + 16, true)});
    p = next;
  }
  if (p !== end) fail();
  return name => {
    const e = entries.get(name);
    if (!e || e.size > MAX_XML) fail();
    const packed = data.subarray(e.content, e.content + e.compressed);
    let unpacked;
    try { unpacked = e.method === 0 ? packed : inflateRawSync(packed, {maxOutputLength: MAX_XML}); } catch { fail(); }
    if (unpacked.length !== e.size || crc32(unpacked) !== e.crc) fail();
    return decoder.decode(unpacked);
  };
}
function xml(source) {
  // No entity expansion, DTDs, external resources or network lookups.
  if (/<!ENTITY/i.test(source)) fail();
  source = source.replace(/<!DOCTYPE[^>]*>/gi, '');
  return new DOMParser({onError: (level) => { if (level !== 'warning') fail(); }}).parseFromString(source, 'application/xml');
}
const elements = (doc, name) => [...doc.getElementsByTagName('*')].filter(e => e.localName === name);
const attr = (node, name) => node.getAttribute(name) || '';
function text(node) {
  if (node.nodeType === 3 || node.nodeType === 4) return node.nodeValue;
  if (node.nodeType !== 1 && node.nodeType !== 9) return '';
  if (['script', 'style', 'svg', 'nav', 'head'].includes(node.localName) || node.hasAttribute?.('hidden') || node.getAttribute?.('aria-hidden') === 'true') return '';
  let out = '';
  for (let child = node.firstChild; child; child = child.nextSibling) out += text(child);
  return out + (['p', 'div', 'section', 'h1', 'h2', 'h3', 'li', 'br'].includes(node.localName) ? '\n' : ' ');
}
export function extractPrefix(raw, cutoff) {
  return extractPrefixFrom(archive(raw), cutoff);
}
export function extractPrefixFrom(get, cutoff) {
  const container = xml(get('META-INF/container.xml'));
  const roots = elements(container, 'rootfile');
  if (roots.length !== 1) fail();
  const opf = pathFor('', attr(roots[0], 'full-path'));
  const doc = xml(get(opf));
  const items = new Map(elements(doc, 'item').map(n => [attr(n, 'id'), n]));
  const spine = elements(doc, 'itemref').map(n => {
    const item = items.get(attr(n, 'idref'));
    if (!item || attr(item, 'media-type') !== 'application/xhtml+xml') fail();
    return {href: pathFor(opf, attr(item, 'href')), nav: attr(item, 'properties').split(/\s+/).includes('nav'), linear: attr(n, 'linear') !== 'no'};
  });
  if (!Number.isInteger(cutoff.spine) || cutoff.spine <= 0 || cutoff.spine >= spine.length ||
      spine[cutoff.spine].href !== cutoff.href || new Set(spine.map(s => s.href)).size !== spine.length) fail();
  let content = '', size = 0, documents = 0;
  for (const item of spine.slice(0, cutoff.spine)) {
    if (!item.linear || item.nav) continue;
    const chapter = xml(get(item.href));
    const body = elements(chapter, 'body');
    if (body.length !== 1) fail();
    const prose = text(body[0]).replace(/[ \t]+/g, ' ').replace(/\n\s*\n+/g, '\n').trim();
    size += new TextEncoder().encode(prose).length;
    if (size > MAX_TEXT) throw new Error('context_too_large');
    if (prose) { content += `\n<completed-document index="${documents++}">\n${prose}\n</completed-document>\n`; }
  }
  if (size < 200) throw new Error('needs_text');
  // Do not send the complete OPF/TOC or any later titles to the model.
  return {content, documents, language: elements(doc, 'language')[0]?.textContent?.slice(0, 64) || '',
    title: elements(doc, 'title')[0]?.textContent?.slice(0, 256) || ''};
}

// Validate each independently compressed member before it enters the cache.
export function unpackEntry(entry, packed) {
  if (packed.length !== entry.compressed || entry.size > MAX_XML) fail();
  let raw;
  try { raw = entry.method === 0 ? packed : inflateRawSync(packed, {maxOutputLength: MAX_XML}); }
  catch { fail(); }
  if (raw.length !== entry.size || crc32(raw) !== entry.crc) fail();
  return decoder.decode(raw);
}
