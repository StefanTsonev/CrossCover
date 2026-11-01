import {digest, readBounded, reply, validate} from './core.js';
import {extractPrefixFrom, pathFor, unpackEntry} from './epub.js';

const DAY = 86400000;
const BLOCK = 16384;
const MAX_MEMBER = 1024 * 1024;
const MAX_BYTES = 8 * MAX_MEMBER;
const CACHE_BYTES = 64 * MAX_MEMBER;
const CACHE_BOOKS = 64;
const MAX_PLANS = 8;
const hash = async data => [...new Uint8Array(await crypto.subtle.digest('SHA-256', data))]
  .map(b => b.toString(16).padStart(2, '0')).join('');
const invalid = () => { throw new Error('unsupported_book'); };
const memberKey = (book, sha) => `member3:${book}:${sha}:`;

async function removePrefix(storage, prefix) {
  for (;;) {
    const batch = await storage.list({prefix, limit: 128});
    if (!batch.size) return;
    await storage.delete([...batch.keys()]);
  }
}
async function removePlan(storage, device) {
  await removePrefix(storage, `pending3:${device}:`);
  await storage.delete(`plan3:${device}`);
}
async function readRange(storage, prefix, offset, length) {
  const out = new Uint8Array(length);
  for (let done = 0; done < length;) {
    const start = Math.floor((offset + done) / BLOCK) * BLOCK;
    const block = await storage.get(prefix + start);
    if (!(block instanceof Uint8Array)) throw new Error('upload_required');
    const local = offset + done - start, n = Math.min(block.length - local, length - done);
    if (n <= 0) throw new Error('upload_required');
    out.set(block.subarray(local, local + n), done); done += n;
  }
  return out;
}
function validateEntries(value) {
  const entries = value.entries;
  if (!Array.isArray(entries) || entries.length < 3 || entries.length > 256) invalid();
  const names = new Set(); let size = 0;
  for (const e of entries) {
    if (typeof e.name !== 'string' || e.name.length > 1024 || pathFor('', e.name) !== e.name || names.has(e.name) ||
        ![0, 8].includes(e.method) || !Number.isInteger(e.size) || e.size < 0 || e.size > MAX_MEMBER ||
        !Number.isInteger(e.compressed) || e.compressed <= 0 || e.compressed > MAX_MEMBER ||
        !Number.isInteger(e.crc) || e.crc < 0 || e.crc > 0xffffffff || !/^[a-f0-9]{64}$/.test(e.sha || '')) invalid();
    names.add(e.name); size += e.compressed;
  }
  if (size > MAX_BYTES) throw new Error('book_too_large');
  if (entries[0].name !== 'META-INF/container.xml' || !entries[1].name.endsWith('.opf')) invalid();
  return entries.map(({name, method, size, compressed, crc, sha}) => ({name, method, size, compressed, crc, sha}));
}

// Count unique not-yet-published hashes across outstanding plans. Two devices
// requesting the same bytes reserve them once; different bytes cannot spend
// the same free capacity. Published bytes are already in the book catalogue.
async function capacity(storage, books, plans, incoming) {
  const groups = new Map();
  for (const plan of [...plans, incoming]) {
    if (!groups.has(plan.bookId)) groups.set(plan.bookId, new Map());
    const hashes = groups.get(plan.bookId);
    for (const entry of plan.entries) hashes.set(entry.sha, Math.max(hashes.get(entry.sha) || 0, entry.compressed));
  }
  let total = books.reduce((n, book) => n + book.bytes, 0);
  const byBook = new Map();
  for (const [id, hashes] of groups) {
    const stored = await storage.get(`member-index3:${id}`) || [];
    const known = new Set(stored);
    let bytes = books.find(b => b.id === id)?.bytes || 0, count = known.size;
    for (const [sha, size] of hashes) if (!known.has(sha)) { bytes += size; total += size; ++count; }
    byBook.set(id, {bytes, count});
  }
  return {total, byBook};
}

// Metadata is bounded independently of book bytes. SHA-256 is over actual
// compressed bytes, not a claimed title, ZIP CRC, filename or reading position.
export async function planMembers(request, storage, device) {
  const value = JSON.parse(await readBounded(request, 65536));
  const entries = validateEntries(value);
  const bookId = await digest(JSON.stringify(entries.slice(0, 2)));
  const id = await digest(JSON.stringify([device, entries]));
  const key = `plan3:${device}`, now = Date.now();
  const previous = await storage.get(key);
  const books = await storage.get('member-books3') || [];
  let book = books.find(b => b.id === bookId);
  let memberList = book ? await storage.get(`member-index3:${bookId}`) || [] : [];
  if (previous?.id === id && previous.expires > now && book?.expires > now &&
      previous.entries.every((e, i) => previous.mask[i] === '1' && !previous.sealed.includes(i) || memberList.includes(e.sha)))
    return reply({status: 'uploading', bookKey: id, missing: previous.mask, offset: previous.offset, total: previous.total});
  await removePlan(storage, device);
  const sorted = [...await storage.list({prefix: 'plan3:'})].sort((a, b) => a[1].expires - b[1].expires);
  for (let i = sorted.length - 1; i >= 0; --i) if (sorted[i][1].expires <= now) {
    await removePlan(storage, sorted[i][0].slice(6)); sorted.splice(i, 1);
  }
  while (sorted.length >= MAX_PLANS) await removePlan(storage, sorted.shift()[0].slice(6));
  const active = sorted.map(([, p]) => p).filter(p => p.missing.some(i => !p.sealed.includes(i)) && books.some(b => b.id === p.bookId && b.expires > now));
  const pinned = new Set(active.map(p => p.bookId));
  const busy = () => reply({status: 'busy'}, 429, {'retry-after': '5'});
  const clearBook = async (old, reset = false) => {
    if (reset) old.bytes = 0;
    else books.splice(books.indexOf(old), 1);
    // Eviction and its accounting are atomic, including removal of the hash
    // index. A restart cannot leave an index claiming deleted data is cached.
    await storage.transaction(async tx => {
      await removePrefix(tx, `member3:${old.id}:`);
      await tx.delete(`member-index3:${old.id}`);
      await tx.put('member-books3', books);
    });
  };
  if (book?.expires <= now) { await clearBook(book); book = null; memberList = []; }
  if (!book) {
    book = {id: bookId, bytes: 0, expires: now + 7 * DAY, used: now}; books.push(book);
  }
  const incoming = {bookId, entries};
  let usage = await capacity(storage, books, active, incoming);
  const required = usage.byBook.get(bookId);
  if (required.bytes > MAX_BYTES || required.count > 512) {
    if (pinned.has(bookId)) return busy();
    await clearBook(book, true); memberList = [];
    usage = await capacity(storage, books, active, incoming);
  }
  for (const old of [...books].sort((a, b) => a.used - b.used)) {
    if (old.id === bookId || pinned.has(old.id)) continue;
    if (old.expires > now && books.length <= CACHE_BOOKS && usage.total <= CACHE_BYTES) break;
    const bytes = old.bytes;
    await clearBook(old); usage.total -= bytes;
  }
  if (books.length > CACHE_BOOKS || usage.total > CACHE_BYTES) return busy();
  const missing = [], mask = entries.map((e, i) => {
    if (memberList.includes(e.sha)) return '0';
    missing.push(i); return '1';
  }).join('');
  const total = missing.reduce((n, i) => n + entries[i].compressed, 0);
  book.used = now; book.expires = now + 7 * DAY;
  await storage.transaction(async tx => {
    await tx.put('member-books3', books);
    await tx.put(key, {id, bookId, entries, missing, mask, total, offset: 0, sealed: [], expires: now + DAY});
  });
  await storage.setAlarm(now + DAY);
  return reply({status: 'uploading', bookKey: id, missing: mask, offset: 0, total});
}

async function sealMembers(storage, device, plan) {
  let start = 0;
  const books = await storage.get('member-books3') || [];
  const book = books.find(b => b.id === plan.bookId);
  if (!book) throw new Error('upload_required');
  const memberList = await storage.get(`member-index3:${plan.bookId}`) || [];
  for (const i of plan.missing) {
    const entry = plan.entries[i], end = start + entry.compressed;
    if (end > plan.offset) break;
    if (!plan.sealed.includes(i) || !memberList.includes(entry.sha)) {
      if (!memberList.includes(entry.sha) && (book.bytes + entry.compressed > MAX_BYTES || memberList.length >= 512 ||
          books.reduce((n, b) => n + b.bytes, 0) + entry.compressed > CACHE_BYTES))
        throw new Error('cache_capacity');
      const packed = await readRange(storage, `pending3:${device}:`, start, entry.compressed);
      if (await hash(packed) !== entry.sha) throw new Error('upload_corrupt');
      unpackEntry(entry, packed); // CRC, inflated-size and UTF-8 checks before publishing.
      if (!memberList.includes(entry.sha)) { memberList.push(entry.sha); book.bytes += packed.length; }
      if (!plan.sealed.includes(i)) plan.sealed.push(i);
      await storage.transaction(async tx => {
        for (let p = 0; p < packed.length; p += BLOCK)
          await tx.put(memberKey(plan.bookId, entry.sha) + p, packed.slice(p, p + BLOCK));
        await tx.put(`member-index3:${plan.bookId}`, memberList);
        await tx.put('member-books3', books);
        await tx.put(`plan3:${device}`, plan);
      });
    }
    start = end;
  }
}

export async function uploadMembers(request, storage, device) {
  const parts = new URL(request.url).pathname.split('/');
  const plan = await storage.get(`plan3:${device}`);
  if (!plan || plan.id !== parts[3] || plan.expires <= Date.now()) return reply({status: 'upload_required'}, 409);
  const offset = Number(parts[5]), length = Number(parts[6]);
  if (offset !== plan.offset || !Number.isSafeInteger(length) || length <= 0 || length > MAX_MEMBER ||
      offset + length > plan.total || offset % BLOCK || (offset + length !== plan.total && length % BLOCK))
    return reply({status: 'upload_conflict', offset: plan.offset}, 409);
  const encoded = request.headers.get('x-recap-encoding') === 'base64';
  const wire = encoded ? Math.ceil(length / 3) * 4 : length;
  const declared = request.headers.get('content-length');
  if (declared !== null && Number(declared) !== wire) return reply({status: 'invalid_length'}, 400);
  const reader = request.body?.getReader();
  if (!reader) return reply({status: 'invalid_request'}, 400);
  let received = 0, decoded = 0, carry = '', padded = false;
  let block = new Uint8Array(BLOCK), filled = 0, pending = [];
  const commit = async () => {
    if (!pending.length) return;
    const next = offset + decoded;
    await storage.transaction(async tx => {
      for (const [at, data] of pending) await tx.put(`pending3:${device}:${at}`, data);
      await tx.put(`plan3:${device}`, {...plan, offset: next});
    });
    plan.offset = next; pending = [];
  };
  const consume = async data => {
    if (data.length > length - decoded) throw new Error('invalid_length');
    for (let p = 0; p < data.length;) {
      const n = Math.min(BLOCK - filled, data.length - p);
      block.set(data.subarray(p, p + n), filled); p += n; filled += n; decoded += n;
      if (filled === BLOCK) {
        pending.push([offset + decoded - filled, block]); block = new Uint8Array(BLOCK); filled = 0;
        if (pending.length === 8) await commit();
      }
    }
  };
  try {
    for (;;) {
      const {done, value} = await reader.read(); if (done) break;
      received += value.length; if (received > wire) throw new Error('invalid_length');
      if (!encoded) await consume(value);
      else {
        // Simulator transport only. Keep carries bounded to one base64 quartet.
        let text = carry + new TextDecoder('ascii', {fatal: true}).decode(value);
        if (/[^A-Za-z0-9+/=]/.test(text) || padded && text.length) throw new Error('invalid_base64');
        const n = text.length - text.length % 4;
        carry = text.slice(n); text = text.slice(0, n);
        if (text) {
          if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(text)) throw new Error('invalid_base64');
          padded = text.includes('='); await consume(Uint8Array.from(atob(text), c => c.charCodeAt(0)));
        }
      }
    }
    if (received !== wire || decoded !== length || carry) throw new Error('invalid_length');
    if (filled) pending.push([offset + decoded - filled, block.slice(0, filled)]);
    await commit();
    await sealMembers(storage, device, plan);
    if (plan.offset === plan.total) await removePrefix(storage, `pending3:${device}:`);
    return reply({status: 'uploading', offset: plan.offset});
  } catch (error) {
    if (error.message === 'cache_capacity') {
      await removePlan(storage, device);
      return reply({status: 'busy'}, 429, {'retry-after': '5'});
    }
    if (error.message === 'upload_corrupt' || error.message === 'unsupported_book') {
      await removePlan(storage, device);
      return reply({status: error.message}, 422);
    }
    return reply({status: 'upload_incomplete', offset: plan.offset}, 409);
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
}

export async function memberPrefix(storage, device, rawValue) {
  const value = validate(rawValue);
  if (value.protocol !== 3) throw new Error('invalid_protocol');
  const plan = await storage.get(`plan3:${device}`);
  if (!plan || plan.id !== value.bookKey || plan.offset !== plan.total || plan.expires <= Date.now())
    throw new Error('upload_required');
  try { await sealMembers(storage, device, plan); }
  catch (error) {
    if (error.message !== 'cache_capacity') throw error;
    await removePlan(storage, device);
    throw new Error('upload_required');
  }
  const books = await storage.get('member-books3') || [];
  const book = books.find(b => b.id === plan.bookId && b.expires > Date.now());
  if (!book) throw new Error('upload_required');
  // Only bounded, validated XML text is made available to the existing server
  // boundary validator. No images, CSS, fonts or future document is required.
  const texts = new Map();
  let bytes = 0;
  for (const entry of plan.entries) {
    const packed = await readRange(storage, memberKey(plan.bookId, entry.sha), 0, entry.compressed);
    const text = unpackEntry(entry, packed); bytes += new TextEncoder().encode(text).length;
    if (bytes > 8 * MAX_MEMBER) throw new Error('context_too_large');
    texts.set(entry.name, text);
  }
  const prefix = extractPrefixFrom(name => {
    if (!texts.has(name)) throw new Error('upload_required');
    return texts.get(name);
  }, value.cutoff);
  // Cache identity is the exact validated model input, independent of device
  // and upload plan so another reader can share the successful recap.
  const sha = await digest(JSON.stringify(prefix));
  return {value, prefix, sha};
}

export async function expireMembers(storage) {
  const now = Date.now();
  for (const [key, plan] of await storage.list({prefix: 'plan3:'}))
    if (plan.expires <= now) await removePlan(storage, key.slice(6));
  const books = await storage.get('member-books3') || [];
  for (const book of books) if (book.expires <= now) {
    await removePrefix(storage, `member3:${book.id}:`);
    await storage.delete(`member-index3:${book.id}`);
  }
  await storage.put('member-books3', books.filter(b => b.expires > now));
  return books.some(b => b.expires > now);
}
