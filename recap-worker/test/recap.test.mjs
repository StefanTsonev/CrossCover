import test from 'node:test';
import assert from 'node:assert/strict';
import {deflateRawSync} from 'node:zlib';
import worker, {RecapStore, MODEL} from '../src/index.js';
import {archive, extractPrefix, pathFor} from '../src/epub.js';
import {CHUNK} from '../src/books.js';
import {keyFor, readBounded, validate} from '../src/core.js';

function crc32(data) {
  let value = 0xffffffff;
  for (const byte of data) { value ^= byte; for (let i = 0; i < 8; i++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0); }
  return (value ^ 0xffffffff) >>> 0;
}
export function zip(files) {
  const locals = [], directory = []; let offset = 0;
  for (const [path, content] of Object.entries(files)) {
    const name = Buffer.from(path), raw = Buffer.from(content), packed = deflateRawSync(raw);
    const local = Buffer.alloc(30); local.writeUInt32LE(0x04034b50); local.writeUInt16LE(8, 8);
    local.writeUInt32LE(crc32(raw), 14); local.writeUInt32LE(packed.length, 18); local.writeUInt32LE(raw.length, 22); local.writeUInt16LE(name.length, 26);
    const central = Buffer.alloc(46); central.writeUInt32LE(0x02014b50); central.writeUInt16LE(8, 10);
    central.writeUInt32LE(crc32(raw), 16); central.writeUInt32LE(packed.length, 20); central.writeUInt32LE(raw.length, 24); central.writeUInt16LE(name.length, 28); central.writeUInt32LE(offset, 42);
    locals.push(local, name, packed); directory.push(central, name); offset += 30 + name.length + packed.length;
  }
  const dir = Buffer.concat(directory), end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50);
  end.writeUInt16LE(directory.length / 2, 8); end.writeUInt16LE(directory.length / 2, 10); end.writeUInt32LE(dir.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, dir, end]);
}
const chapter = marker => `<html xmlns="http://www.w3.org/1999/xhtml"><head><title>ignore</title></head><body><h1>${marker}</h1><p>${'Alice followed the rabbit and found a locked door. '.repeat(30)}</p><script>HIDDEN_SCRIPT</script></body></html>`;
export const files = () => ({'META-INF/container.xml': '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>',
  'OPS/book.opf': '<package xmlns:dc="http://purl.org/dc/elements/1.1/"><metadata><dc:title>Fixture</dc:title><dc:language>en</dc:language></metadata><manifest><item id="a" href="one.xhtml" media-type="application/xhtml+xml"/><item id="b" href="two.xhtml" media-type="application/xhtml+xml"/><item id="c" href="three.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/><itemref idref="c"/></spine></package>',
  'OPS/one.xhtml': chapter('PAST_MARKER'), 'OPS/two.xhtml': chapter('CURRENT_MARKER'), 'OPS/three.xhtml': chapter('FUTURE_MARKER')});
function fingerprint(raw) { let hash = 14695981039346656037n; for (const b of raw) hash = BigInt.asUintN(64, (hash ^ BigInt(b)) * 1099511628211n); return hash.toString(16).padStart(16, '0'); }
function setup() {
  const map = new Map();
  const storage = {get: async k => structuredClone(map.get(k)), put: async (k, v) => { map.set(k, structuredClone(v)); },
    delete: async keys => { for (const k of Array.isArray(keys) ? keys : [keys]) map.delete(k); }, setAlarm: async () => {},
    list: async ({prefix = '', limit = 1000} = {}) => new Map([...map].filter(([k]) => k.startsWith(prefix)).slice(0, limit))};
  storage.transaction = async fn => fn(storage);
  const env = {GEMINI_API_KEY: 'fixture-secret'};
  const store = new RecapStore({storage}, env); env.RECAPS = {idFromName: s => s, get: () => store};
  const request = (path, method = 'GET', body, token = 'a', extra = {}) => worker.fetch(new Request('https://recap.test' + path, {
    method, headers: {'x-recap-device': token === 'b' ? 'reader2' : token === 'a' ? 'reader1' : token, ...extra}, body: body instanceof Uint8Array || body instanceof ReadableStream ? body : body === undefined ? undefined : JSON.stringify(body), duplex: 'half'
  }), env);
  const raw = zip(files()), key = fingerprint(raw);
  const value = {protocol: 2, bookKey: key, cutoff: {spine: 1, href: 'OPS/two.xhtml'}, summarizeThroughChapter: 0, throughTitle: 'The first chapter'};
  const upload = async (data = raw, token = 'a') => {
    const key = fingerprint(data);
    let r = await request('/v2/book', 'POST', {key, size: data.length}, token); assert.equal(r.status, 200);
    const begin = await r.json();
    for (let offset = begin.offset; offset < data.length; offset += CHUNK) {
      r = await request(`/v2/book/${key}/${offset}`, 'PUT', data.subarray(offset, offset + CHUNK), token); assert.equal(r.status, 200);
    }
    r = await request(`/v2/book/${key}/complete`, 'POST', undefined, token); assert.equal(r.status, 200);
  };
  return {map, storage, env, store, request, raw, key, value, upload};
}
function mockGemini(t, handler) {
  const calls = {count: 0, generate: 0};
  t.mock.method(globalThis, 'fetch', async (url, options) => {
    assert.match(url, new RegExp(`${MODEL}:`));
    const payload = JSON.parse(options.body); assert.equal(payload.tools, undefined);
    const data = JSON.stringify(payload); assert.ok(!data.includes('CURRENT_MARKER')); assert.ok(!data.includes('FUTURE_MARKER'));
    assert.ok(data.includes('PAST_MARKER')); assert.ok(!data.includes('HIDDEN_SCRIPT'));
    if (url.endsWith(':countTokens')) { calls.count++; return new Response(JSON.stringify({totalTokens: 800})); }
    calls.generate++;
    if (handler) return handler(payload);
    return new Response(JSON.stringify({candidates: [{finishReason: 'STOP', content: {parts: [{text: JSON.stringify({summary: 'Alice follows a rabbit into a strange place and discovers a locked door. '.repeat(12)})}]}}]}));
  });
  return calls;
}
test('server excludes current and future documents before any model call', () => {
  const prefix = extractPrefix(zip(files()), {spine: 1, href: 'OPS/two.xhtml'});
  assert.equal(prefix.documents, 1); assert.match(prefix.content, /PAST_MARKER/);
  for (const marker of ['CURRENT_MARKER', 'FUTURE_MARKER', 'HIDDEN_SCRIPT']) assert.ok(!prefix.content.includes(marker));
});
test('wrong file, anchors, first chapter, duplicate spine and traversal fail closed', () => {
  for (const cutoff of [{spine: 0, href: 'OPS/one.xhtml'}, {spine: 1, href: 'OPS/three.xhtml'}, {spine: 1, href: 'OPS/two.xhtml#later'}])
    assert.throws(() => extractPrefix(zip(files()), cutoff));
  for (const href of ['../../secret', 'https://example.com/book', '%2e%2e/%2e%2e/book', 'x%5Cy']) assert.throws(() => pathFor('OPS/book.opf', href));
  const f = files(); f['OPS/book.opf'] = f['OPS/book.opf'].replace('href="two.xhtml"', 'href="one.xhtml"');
  assert.throws(() => extractPrefix(zip(f), {spine: 1, href: 'OPS/one.xhtml'}));
});
test('CRC damage, encrypted archives, XML entities and expansion bombs are rejected', () => {
  const raw = zip(files()); const broken = Buffer.from(raw); broken[55] ^= 1;
  assert.throws(() => extractPrefix(broken, {spine: 1, href: 'OPS/two.xhtml'}));
  const f = files(); f['OPS/one.xhtml'] = '<!DOCTYPE html [<!ENTITY x "secret">]><html><body>&x;</body></html>';
  assert.throws(() => extractPrefix(zip(f), {spine: 1, href: 'OPS/two.xhtml'}));
  assert.throws(() => archive(zip({'bomb': 'x'.repeat(1024 * 1024 + 1)}))('bomb'));
  const encrypted = Buffer.from(raw); encrypted.writeUInt16LE(1, 6); assert.throws(() => archive(encrypted));
});
test('bounded request and protocol reject old metadata-only requests', async () => {
  assert.throws(() => validate({book: {title: 'old'}, chapters: []}));
  await assert.rejects(readBounded(new Response('x'.repeat(16385))), /too_large/);
});
test('public upload resumes at acknowledged offset and rejects duplicate/out-of-order chunks', async () => {
  const f = setup(), raw = Buffer.concat([f.raw, Buffer.alloc(35000)]), key = fingerprint(raw);
  assert.equal((await f.request('/v2/book', 'POST', {key, size: raw.length}, 'invalid/id')).status, 400);
  await f.request('/v2/book', 'POST', {key, size: raw.length});
  assert.equal((await f.request(`/v2/book/${key}/16384`, 'PUT', raw.subarray(0, CHUNK))).status, 409);
  assert.equal((await f.request(`/v2/book/${key}/0`, 'PUT', raw.subarray(0, CHUNK))).status, 200);
  assert.equal((await (await f.request('/v2/book', 'POST', {key, size: raw.length})).json()).offset, CHUNK);
  assert.equal((await f.request(`/v2/book/${key}/0`, 'PUT', raw.subarray(0, CHUNK))).status, 409);
  assert.equal((await f.request(`/v2/book/${key}/complete`, 'POST')).status, 409);
});
test('binary and simulator base64 transports preserve exact EPUB bytes', async () => {
  const f = setup(); await f.request('/v2/book', 'POST', {key: f.key, size: f.raw.length});
  const response = await worker.fetch(new Request(`https://recap.test/v2/book/${f.key}/0`, {method: 'PUT',
    headers: {'x-recap-device': 'reader1', 'x-recap-encoding': 'base64'}, body: f.raw.toString('base64')}), f.env);
  assert.equal(response.status, 200);
  assert.equal((await f.request(`/v2/book/${f.key}/complete`, 'POST')).status, 200);
});
test('one text generation is shared across users and survives object restart', async t => {
  const f = setup(), calls = mockGemini(t); await f.upload();
  let r = await f.request('/v2/recaps', 'POST', f.value); assert.equal(r.status, 200); assert.equal((await r.json()).origin, 'epub_text');
  await f.upload(f.raw, 'b');
  r = await f.request('/v2/recaps', 'POST', f.value, 'b'); assert.equal(r.status, 200);
  const restored = new RecapStore({storage: f.storage}, f.env);
  assert.equal((await restored.fetch(new Request('https://internal/v2/recaps', {method: 'POST', headers: {'x-recap-device': 'reader1'}, body: JSON.stringify(f.value)}))).status, 200);
  assert.equal(calls.generate, 1); assert.equal(calls.count, 1);
});
test('cache identity includes SHA, boundary, model and prompt version', async () => {
  const a = {sha: 'one', cutoff: {spine: 1, href: 'one'}};
  assert.notEqual(await keyFor(a, MODEL), await keyFor({...a, sha: 'two'}, MODEL));
  assert.notEqual(await keyFor(a, MODEL), await keyFor(a, 'other-model'));
});
test('provider 429 creates shared cooldown without automatic fallback or generation retries', async t => {
  const f = setup(), calls = mockGemini(t, () => new Response('{}', {status: 429, headers: {'retry-after': '90'}})); await f.upload();
  assert.equal((await f.request('/v2/recaps', 'POST', f.value)).status, 429);
  assert.equal((await f.request('/v2/recaps', 'POST', f.value)).status, 429); assert.equal(calls.generate, 1);
});

test('daily quota type survives shared cooldown and object restart', async t => {
  const error = {error: {details: [{'@type': 'type.googleapis.com/google.rpc.QuotaFailure',
    violations: [{quotaId: 'GenerateRequestsPerDayPerProjectPerModel-FreeTier'}]}]}};
  const f = setup(), calls = mockGemini(t, () => new Response(JSON.stringify(error), {status: 429}));
  await f.upload();
  let r = await f.request('/v2/recaps', 'POST', f.value);
  assert.equal(r.status, 429); assert.equal((await r.json()).quota, 'daily');
  const restored = new RecapStore({storage: f.storage}, f.env);
  r = await restored.fetch(new Request('https://internal/v2/recaps', {method: 'POST',
    headers: {'x-recap-device': 'reader1'}, body: JSON.stringify(f.value)}));
  const info = await r.json();
  assert.equal(info.quota, 'daily'); assert.equal(info.retryAfter, undefined);
  assert.equal(calls.generate, 1);
});

test('countTokens 429 exposes explicit minute retry info without attempting generation', async t => {
  const f = setup(); await f.upload(); let count = 0;
  t.mock.method(globalThis, 'fetch', async url => {
    assert.match(url, /:countTokens$/); ++count;
    return new Response(JSON.stringify({error: {details: [
      {'@type': 'type.googleapis.com/google.rpc.QuotaFailure', violations: [{quotaId: 'GenerateContentInputTokensPerModelPerMinute-FreeTier'}]},
      {'@type': 'type.googleapis.com/google.rpc.RetryInfo', retryDelay: '44.2s'}
    ]}}), {status: 429});
  });
  let r = await f.request('/v2/recaps', 'POST', f.value), info = await r.json();
  assert.equal(info.quota, 'minute'); assert.equal(info.retryAfter, 45);
  r = await f.request('/v2/recaps', 'POST', f.value); info = await r.json();
  assert.equal(info.quota, 'minute'); assert.ok(info.retryAfter > 0 && info.retryAfter <= 45);
  assert.equal(count, 1);
});
test('unknown generation outcome is not automatically charged twice', async t => {
  const f = setup(), calls = mockGemini(t, () => { throw new Error('timeout'); }); await f.upload();
  assert.equal((await f.request('/v2/recaps', 'POST', f.value)).status, 422);
  assert.equal((await f.request('/v2/recaps', 'POST', f.value)).status, 409); assert.equal(calls.generate, 1);
});
test('wrong boundary and incomplete uploads spend no Gemini calls', async t => {
  const f = setup(), calls = mockGemini(t);
  assert.equal((await f.request('/v2/recaps', 'POST', f.value)).status, 409);
  await f.upload(); const invalid = {...f.value, cutoff: {spine: 1, href: 'OPS/three.xhtml'}};
  assert.equal((await f.request('/v2/recaps', 'POST', invalid)).status, 422); assert.equal(calls.count, 0); assert.equal(calls.generate, 0);
});
test('books expire and replacing a device book removes its old chunks', async () => {
  const f = setup(); await f.upload(); const old = f.map.get('book:reader1'); old.expires = Date.now() - 1; f.map.set('book:reader1', old);
  await f.store.alarm(); assert.equal([...f.map.keys()].filter(k => k.startsWith('chunk:')).length, 0);
  assert.equal(f.map.has('book:reader1'), false);
});
test('usage is informational and has no app daily cap', async () => {
  const f = setup(); const data = await (await f.request('/v1/usage')).json(); assert.equal(data.dailyLimit, null); assert.equal(data.model, MODEL);
});
test('large token count refuses generation rather than dropping early chapters', async t => {
  const f = setup(); await f.upload(); let calls = 0;
  t.mock.method(globalThis, 'fetch', async url => {
    calls++; assert.match(url, /:countTokens$/); return new Response(JSON.stringify({totalTokens: 200001}));
  });
  const response = await f.request('/v2/recaps', 'POST', f.value);
  assert.equal(response.status, 413); assert.equal((await response.json()).status, 'context_too_large'); assert.equal(calls, 1);
});
test('high aggregate usage does not prevent another uncached generation', async t => {
  const f = setup(), calls = mockGemini(t); await f.upload();
  f.map.set('usage:' + new Date().toISOString().slice(0, 10), 9999);
  assert.equal((await f.request('/v2/recaps', 'POST', f.value)).status, 200); assert.equal(calls.generate, 1);
});
test('concurrent mutations cannot overwrite the uploaded book during a generation', async t => {
  const f = setup(); await f.upload(); let release, started;
  const waiting = new Promise(resolve => { started = resolve; });
  mockGemini(t, () => { started(); return new Promise(resolve => { release = () => resolve(new Response('{}', {status: 503})); }); });
  const first = f.request('/v2/recaps', 'POST', f.value); await waiting;
  assert.equal((await f.request('/v2/book', 'POST', {key: 'f'.repeat(16), size: 100})).status, 429);
  assert.equal(f.map.get('book:reader1').key, f.key); release(); await first;
});
test('corrupt upload cannot be sealed or reused', async () => {
  const f = setup(); await f.request('/v2/book', 'POST', {key: f.key, size: f.raw.length});
  const raw = Buffer.from(f.raw); raw[100] ^= 1;
  assert.equal((await f.request(`/v2/book/${f.key}/0`, 'PUT', raw)).status, 200);
  assert.equal((await f.request(`/v2/book/${f.key}/complete`, 'POST')).status, 422);
  assert.equal(f.map.has('book:reader1'), false);
  assert.equal([...f.map.keys()].some(k => k.startsWith('chunk:reader1:')), false);
});
test('overlong or unfinished model response never becomes a saved ready recap', async t => {
  const f = setup(); await f.upload();
  mockGemini(t, () => new Response(JSON.stringify({candidates: [{finishReason: 'MAX_TOKENS', content: {parts: [{text: JSON.stringify({summary: 'word '.repeat(310)})}]}}]})));
  const response = await f.request('/v2/recaps', 'POST', f.value);
  assert.equal(response.status, 422); assert.equal((await response.json()).status, 'needs_text');
});
test('one streaming PUT stores a multi-block book with exact bytes', async () => {
  const f = setup(), raw = Buffer.concat([f.raw, Buffer.alloc(300000, 7)]), key = fingerprint(raw);
  await f.request('/v2/book', 'POST', {key, size: raw.length});
  let cursor = 0;
  const stream = new ReadableStream({pull(controller) {
    if (cursor === raw.length) return controller.close();
    const next = Math.min(raw.length, cursor + 5113);
    controller.enqueue(raw.subarray(cursor, next)); cursor = next;
  }});
  const response = await f.request(`/v2/book/${key}/stream/0`, 'PUT', stream);
  assert.equal(response.status, 200); assert.equal((await response.json()).offset, raw.length);
  assert.equal((await f.request(`/v2/book/${key}/complete`, 'POST')).status, 200);
  const recovered = [];
  for (let i = 0; i < raw.length; i += CHUNK) recovered.push(f.map.get(`chunk:reader1:${i}`));
  assert.deepEqual(Buffer.concat(recovered), raw);
});
test('interrupted stream retains only durable checkpoints and resumes safely', async () => {
  const f = setup(), raw = Buffer.concat([f.raw, Buffer.alloc(300000, 9)]), key = fingerprint(raw);
  await f.request('/v2/book', 'POST', {key, size: raw.length});
  let step = 0;
  const interrupted = new ReadableStream({pull(controller) {
    if (!step++) controller.enqueue(raw.subarray(0, 150000));
    else controller.error(new Error('disconnected'));
  }});
  const first = await f.request(`/v2/book/${key}/stream/0`, 'PUT', interrupted);
  assert.equal(first.status, 409); assert.equal((await first.json()).offset, 131072);
  const begin = await (await f.request('/v2/book', 'POST', {key, size: raw.length})).json();
  assert.equal(begin.offset, 131072);
  assert.equal((await f.request(`/v2/book/${key}/stream/0`, 'PUT', raw)).status, 409);
  assert.equal((await f.request(`/v2/book/${key}/stream/${begin.offset}`, 'PUT', raw.subarray(begin.offset))).status, 200);
  assert.equal((await f.request(`/v2/book/${key}/complete`, 'POST')).status, 200);
});
test('truncated and oversized streams cannot seal the book', async () => {
  const f = setup(); await f.request('/v2/book', 'POST', {key: f.key, size: f.raw.length});
  assert.equal((await f.request(`/v2/book/${f.key}/stream/0`, 'PUT', f.raw.subarray(0, 100))).status, 409);
  assert.equal((await f.request(`/v2/book/${f.key}/complete`, 'POST')).status, 409);
  assert.equal((await f.request(`/v2/book/${f.key}/stream/0`, 'PUT', Buffer.concat([f.raw, Buffer.from([1])]))).status, 413);
  assert.equal(f.map.get('book:reader1').offset, 0);
});
test('simulator base64 stream handles arbitrary network splits and validates its full fingerprint', async () => {
  const f = setup(), raw = Buffer.concat([f.raw, Buffer.alloc(170000, 5)]), key = fingerprint(raw);
  await f.request('/v2/book', 'POST', {key, size: raw.length});
  const encoded = Buffer.from(raw.toString('base64')); let offset = 0;
  const stream = new ReadableStream({pull(controller) {
    if (offset === encoded.length) return controller.close();
    const next = Math.min(encoded.length, offset + 7331);
    controller.enqueue(encoded.subarray(offset, next)); offset = next;
  }});
  const response = await f.request(`/v2/book/${key}/stream/0`, 'PUT', stream, 'a', {'x-recap-encoding': 'base64'});
  assert.equal(response.status, 200);
  assert.equal((await f.request(`/v2/book/${key}/complete`, 'POST')).status, 200);
});
test('stream rejects wrong declared length and malformed base64 before sealing', async () => {
  const f = setup(); await f.request('/v2/book', 'POST', {key: f.key, size: f.raw.length});
  assert.equal((await f.request(`/v2/book/${f.key}/stream/0`, 'PUT', f.raw, 'a', {'content-length': '2'})).status, 400);
  const stream = new ReadableStream({start(controller) { controller.enqueue(Buffer.from('!!!!')); controller.close(); }});
  assert.equal((await f.request(`/v2/book/${f.key}/stream/0`, 'PUT', stream, 'a', {'x-recap-encoding': 'base64'})).status, 409);
  assert.equal(f.map.get('book:reader1').offset, 0);
});
test('bounded streaming segments resume from the last completed segment', async () => {
  const f = setup(), raw = Buffer.concat([f.raw, Buffer.alloc(2200000, 3)]), key = fingerprint(raw);
  await f.request('/v2/book', 'POST', {key, size: raw.length});
  const first = await f.request(`/v2/book/${key}/stream/0/1048576`, 'PUT', raw.subarray(0, 1048576));
  assert.equal(first.status, 200); assert.equal((await first.json()).offset, 1048576);
  // An interrupted request that never reaches the Worker loses this segment
  // only; the completed first segment remains durable across a new instance.
  const restored = new RecapStore({storage: f.storage}, f.env);
  const resumed = await restored.fetch(new Request('https://internal/v2/book', {method: 'POST', headers: {'x-recap-device': 'reader1'},
    body: JSON.stringify({key, size: raw.length})}));
  assert.equal((await resumed.json()).offset, 1048576);
  for (let offset = 1048576; offset < raw.length; offset += 1048576) {
    const length = Math.min(1048576, raw.length - offset);
    assert.equal((await f.request(`/v2/book/${key}/stream/${offset}/${length}`, 'PUT', raw.subarray(offset, offset + length))).status, 200);
  }
  assert.equal((await f.request(`/v2/book/${key}/complete`, 'POST')).status, 200);
});
test('bounded segments reject non-aligned and out-of-range boundaries', async () => {
  const f = setup(), raw = Buffer.alloc(40000), key = fingerprint(raw);
  await f.request('/v2/book', 'POST', {key, size: raw.length});
  assert.equal((await f.request(`/v2/book/${key}/stream/0/20000`, 'PUT', raw.subarray(0, 20000))).status, 400);
  assert.equal((await f.request(`/v2/book/${key}/stream/0/65536`, 'PUT', raw)).status, 400);
  assert.equal(f.map.get('book:reader1').offset, 0);
});

test('public usage and cached recap reads need no credentials; uploads need only an identifier', async () => {
  const f = setup();
  assert.equal((await worker.fetch(new Request('https://recap.test/v1/usage'), f.env)).status, 200);
  assert.equal((await worker.fetch(new Request('https://recap.test/v2/book', {method:'POST',body:JSON.stringify({key:f.key,size:f.raw.length})}), f.env)).status, 400);
  assert.equal((await f.request('/v2/book','POST',{key:f.key,size:f.raw.length},'new_public_reader')).status, 200);
});
