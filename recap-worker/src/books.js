import {MAX_EPUB} from './epub.js';
import {readBounded, reply} from './core.js';
export const CHUNK = 16384;
const DAY = 86400000;
const CHECKPOINT = 128 * 1024;

// One HTTP body, many durable checkpoints. A failed connection loses at most
// the uncommitted checkpoint, without needing a round trip per storage block.
async function streamUpload(request, storage, device, book, start, length = book.size - start) {
  if (start !== book.offset || book.sha) return reply({status: 'upload_conflict', offset: book.offset}, 409);
  const end = start + length;
  if (!Number.isSafeInteger(length) || length <= 0 || end > book.size || end !== book.size && end % CHUNK)
    return reply({status: 'invalid_length', offset: book.offset}, 400);
  const encoded = request.headers.get('x-recap-encoding') === 'base64';
  const expected = length;
  const declared = request.headers.get('content-length');
  if (declared !== null && Number(declared) !== (encoded ? Math.ceil(expected / 3) * 4 : expected))
    return reply({status: 'invalid_length', offset: book.offset}, 400);
  const reader = request.body?.getReader();
  if (!reader || !expected) return reply({status: 'invalid_request'}, 400);
  let offset = start, block = new Uint8Array(CHUNK), used = 0, pending = [], pendingBytes = 0, carry = '';
  const commit = async () => {
    if (!pending.length) return;
    const saved = {...book, offset};
    await storage.transaction(async tx => {
      for (const [key, value] of pending) await tx.put(key, value);
      await tx.put(`book:${device}`, saved);
    });
    book.offset = offset;
    pending = []; pendingBytes = 0;
  };
  const consume = async value => {
    if (value.length > end - offset - used) throw new Error('too_large');
    for (let position = 0; position < value.length;) {
      const count = Math.min(CHUNK - used, value.length - position);
      block.set(value.subarray(position, position + count), used); used += count; position += count;
      if (used === CHUNK) {
        pending.push([`chunk:${device}:${offset}`, block]); offset += used; pendingBytes += used;
        block = new Uint8Array(CHUNK); used = 0;
        if (pendingBytes >= CHECKPOINT) await commit();
      }
    }
  };
  try {
    for (;;) {
      const {done, value} = await reader.read(); if (done) break;
      if (encoded) {
        // Simulator HTTP shims accept text bodies only. Production uses binary.
        const text = carry + new TextDecoder('utf-8', {fatal: true}).decode(value);
        const length = text.length - text.length % 4;
        const complete = text.slice(0, length); carry = text.slice(length);
        if (!/^[A-Za-z0-9+/]*={0,2}$/.test(complete) || complete.includes('=') && carry.length) throw new Error('invalid_base64');
        if (complete) await consume(Uint8Array.from(atob(complete), c => c.charCodeAt(0)));
        if (complete.includes('=')) {
          const tail = await reader.read(); if (!tail.done) throw new Error('invalid_base64');
          break;
        }
      } else await consume(value);
    }
    if (carry || offset + used !== end) return reply({status: 'upload_incomplete', offset: book.offset}, 409);
    if (used) { pending.push([`chunk:${device}:${offset}`, block.slice(0, used)]); offset += used; }
    await commit();
    return reply({status: 'uploaded', offset: book.offset});
  } catch (error) {
    return reply({status: error.message === 'too_large' ? 'too_large' : 'upload_incomplete', offset: book.offset}, error.message === 'too_large' ? 413 : 409);
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
}
export async function clearBook(storage, device) {
  for (;;) {
    const keys = [...(await storage.list({prefix: `chunk:${device}:`, limit: 128})).keys()];
    if (!keys.length) break;
    await storage.delete(keys);
  }
  await storage.delete(`book:${device}`);
}
export async function readBook(storage, device, book) {
  const raw = new Uint8Array(book.size);
  for (let offset = 0; offset < book.size; offset += CHUNK) {
    const chunk = await storage.get(`chunk:${device}:${offset}`);
    if (!(chunk instanceof Uint8Array) || chunk.length !== Math.min(CHUNK, book.size - offset)) throw new Error('upload_required');
    raw.set(chunk, offset);
  }
  return raw;
}
export async function upload(request, storage, device) {
  const parts = new URL(request.url).pathname.split('/');
  let book = await storage.get(`book:${device}`);
  if (request.method === 'POST' && parts.length === 3) {
    const value = JSON.parse(await readBounded(request, 512));
    if (!/^[a-f0-9]{16}$/.test(value.key || '') || !Number.isInteger(value.size) || value.size < 22 || value.size > MAX_EPUB)
      return reply({status: 'book_too_large'}, 413);
    if (book?.key !== value.key || book.size !== value.size || book.expires <= Date.now()) {
      await clearBook(storage, device);
      book = {key: value.key, size: value.size, offset: 0, expires: Date.now() + 7 * DAY};
      await storage.put(`book:${device}`, book);
    }
    await storage.setAlarm(Date.now() + DAY);
    return reply({status: book.sha ? 'ready' : 'uploading', offset: book.offset});
  }
  if (!book || book.key !== parts[3] || book.expires <= Date.now()) return reply({status: 'upload_required'}, 409);
  if (request.method === 'PUT' && parts[4] === 'stream' && /^\d+$/.test(parts[5] || '') &&
      (parts.length === 6 || parts.length === 7 && /^\d+$/.test(parts[6]) && Number(parts[6]) <= 1048576))
    return streamUpload(request, storage, device, book, Number(parts[5]), parts.length === 7 ? Number(parts[6]) : undefined);
  if (request.method === 'PUT' && /^\d+$/.test(parts[4] || '') && parts.length === 5) {
    const offset = Number(parts[4]);
    const encoded = request.headers.get('x-recap-encoding') === 'base64';
    const maximum = encoded ? 21848 : CHUNK;
    if (Number(request.headers.get('content-length')) > maximum) return reply({status: 'too_large'}, 413);
    // Bounded streaming read: do not trust the Content-Length header.
    const reader = request.body?.getReader();
    if (!reader) return reply({status: 'invalid_request'}, 400);
    let buffer = new Uint8Array(maximum); let length = 0;
    try {
      for (;;) {
        const {done, value} = await reader.read(); if (done) break;
        if (value.length > maximum - length) return reply({status: 'too_large'}, 413);
        buffer.set(value, length); length += value.length;
      }
    } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
    if (encoded) {
      const value = new TextDecoder().decode(buffer.subarray(0, length));
      if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value)) return reply({status: 'invalid_request'}, 400);
      buffer = Uint8Array.from(atob(value), c => c.charCodeAt(0)); length = buffer.length;
    }
    if (offset !== book.offset || book.sha || length !== Math.min(CHUNK, book.size - offset))
      return reply({status: 'upload_conflict', offset: book.offset}, 409);
    book.offset += length;
    await storage.transaction(async tx => {
      await tx.put(`chunk:${device}:${offset}`, buffer.slice(0, length));
      await tx.put(`book:${device}`, book);
    });
    return reply({status: 'uploading', offset: book.offset});
  }
  if (request.method === 'POST' && parts[4] === 'complete' && parts.length === 5) {
    if (book.offset !== book.size) return reply({status: 'upload_required'}, 409);
    if (!book.sha) {
      const raw = await readBook(storage, device, book);
      // Check the device's streaming fingerprint as well as an authoritative
      // SHA-256 for cache identity. FNV is never used for authentication.
      let hash = 14695981039346656037n;
      for (const byte of raw) hash = BigInt.asUintN(64, (hash ^ BigInt(byte)) * 1099511628211n);
      if (hash.toString(16).padStart(16, '0') !== book.key) {
        await clearBook(storage, device);
        return reply({status: 'upload_corrupt'}, 422);
      }
      book.sha = [...new Uint8Array(await crypto.subtle.digest('SHA-256', raw))].map(b => b.toString(16).padStart(2, '0')).join('');
      await storage.put(`book:${device}`, book);
    }
    return reply({status: 'ready'});
  }
  return reply({status: 'not_found'}, 404);
}
