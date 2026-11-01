import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash, randomBytes} from 'node:crypto';
import {deflateRawSync} from 'node:zlib';
import worker, {RecapStore} from '../src/index.js';
import {crc32} from '../src/epub.js';

const sha = b => createHash('sha256').update(b).digest('hex');
const chapter = marker => `<html><body><h1>${marker}</h1><p>${'The traveller crossed the river and found a deserted village. '.repeat(30)}</p></body></html>`;
function fixture() {
  const files = {
    'META-INF/container.xml': '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>',
    'OPS/book.opf': '<package xmlns:dc="urn:dc"><metadata><dc:title>Member fixture</dc:title><dc:language>en</dc:language></metadata><manifest>' +
      ['one', 'two', 'three'].map(n => `<item id="${n}" href="${n}.xhtml" media-type="application/xhtml+xml"/>`).join('') +
      '</manifest><spine><itemref idref="one"/><itemref idref="two"/><itemref idref="three"/></spine></package>',
    'OPS/one.xhtml': chapter('PAST_MEMBER'), 'OPS/two.xhtml': chapter('SECOND_MEMBER'), 'OPS/three.xhtml': chapter('FUTURE_MEMBER')
  };
  return Object.entries(files).map(([name, text]) => {
    const raw = Buffer.from(text), packed = deflateRawSync(raw);
    return {entry: {name, size: raw.length, compressed: packed.length, method: 8, crc: crc32(raw), sha: sha(packed)}, packed};
  });
}
function harness() {
  const data = new Map();
  const storage = {
    get: async k => structuredClone(data.get(k)),
    put: async (k, value) => { data.set(k, structuredClone(value)); },
    delete: async keys => { for (const key of Array.isArray(keys) ? keys : [keys]) data.delete(key); },
    list: async ({prefix = '', limit = 1000} = {}) => new Map([...data].filter(([k]) => k.startsWith(prefix)).sort().slice(0, limit)),
    setAlarm: async () => {}
  };
  storage.transaction = fn => fn(storage);
  const env = {GEMINI_API_KEY: 'fixture-only'};
  let store = new RecapStore({storage}, env);
  env.RECAPS = {idFromName: x => x, get: () => store};
  const send = (path, method = 'POST', body, device = 'reader', extra = {}) => worker.fetch(new Request('https://recap.test' + path, {
    method, headers: {'x-recap-device': device, ...extra},
    body: body instanceof Uint8Array || body instanceof ReadableStream || typeof body === 'string' ? body : JSON.stringify(body), duplex: 'half'
  }), env);
  return {data, storage, send, restart: () => { store = new RecapStore({storage}, env); }};
}
async function plan(h, members, device = 'reader') {
  const r = await h.send('/v3/plan', 'POST', {entries: members.map(m => m.entry)}, device);
  assert.equal(r.status, 200, await r.clone().text());
  return r.json();
}
async function upload(h, members, device = 'reader', encoded = false) {
  const p = await plan(h, members, device);
  const body = Buffer.concat(members.filter((_, i) => p.missing[i] === '1').map(m => m.packed));
  for (let offset = p.offset; offset < body.length; offset += 1048576) {
    const bytes = body.subarray(offset, offset + 1048576);
    const r = await h.send(`/v3/book/${p.bookKey}/stream/${offset}/${bytes.length}`, 'PUT',
      encoded ? bytes.toString('base64') : bytes, device, encoded ? {'x-recap-encoding': 'base64'} : {});
    assert.equal(r.status, 200, await r.clone().text());
  }
  return p;
}
const recap = (p, spine = 1) => ({protocol: 3, bookKey: p.bookKey,
  cutoff: {spine, href: `OPS/${spine === 1 ? 'two' : 'three'}.xhtml`}, throughTitle: 'Last completed chapter', summarizeThroughChapter: spine - 1});
function mockModel(t) {
  const calls = [];
  t.mock.method(globalThis, 'fetch', async (url, options) => {
    const payload = JSON.parse(options.body);
    const text = JSON.stringify(payload);
    assert.ok(text.includes('PAST_MEMBER')); assert.ok(!text.includes('FUTURE_MEMBER'));
    if (url.endsWith(':countTokens')) return new Response(JSON.stringify({totalTokens: 900}));
    calls.push(payload);
    return new Response(JSON.stringify({candidates: [{finishReason: 'STOP', content: {parts: [{text: JSON.stringify({
      summary: 'The traveller crosses the river and discovers a deserted village. '.repeat(12)})}]}}]}));
  });
  return calls;
}

test('member upload excludes future text and images, generates once, and shares cache across devices', async t => {
  const h = harness(), members = fixture().slice(0, 3), calls = mockModel(t);
  const p = await upload(h, members);
  let r = await h.send('/v3/recaps', 'POST', recap(p));
  assert.equal(r.status, 200, await r.clone().text());
  const first = await r.json(); assert.equal(first.documents, 1); assert.equal(calls.length, 1);
  const q = await plan(h, members, 'other');
  assert.equal(q.total, 0); assert.equal(q.missing, '000');
  h.restart(); r = await h.send('/v3/recaps', 'POST', recap(q), 'other');
  assert.equal(r.status, 200); assert.equal((await r.json()).id, first.id); assert.equal(calls.length, 1);
  assert.ok(!JSON.stringify(calls[0]).includes('SECOND_MEMBER'));
});

test('advancing a chapter uploads only the additional compressed member', async t => {
  const h = harness(), all = fixture(), calls = mockModel(t);
  await upload(h, all.slice(0, 3));
  const p = await upload(h, all.slice(0, 4));
  assert.equal(p.missing, '0001'); assert.equal(p.total, all[3].packed.length);
  const r = await h.send('/v3/recaps', 'POST', recap(p, 2));
  assert.equal(r.status, 200); assert.equal((await r.json()).documents, 2);
  assert.ok(JSON.stringify(calls[0]).includes('SECOND_MEMBER'));
});

test('switching books retains earlier uploaded members', async () => {
  const h = harness(), first = fixture().slice(0, 3), other = fixture().slice(0, 3);
  const raw = Buffer.from('<package>different edition</package>'); other[1].packed = deflateRawSync(raw);
  Object.assign(other[1].entry, {size: raw.length, compressed: other[1].packed.length, crc: crc32(raw), sha: sha(other[1].packed)});
  await upload(h, first); await upload(h, other);
  assert.equal((await plan(h, first)).total, 0);
  assert.equal((await h.storage.get('member-books3')).length, 2);
});

test('missing required document and wrong chapter cutoff never reach Gemini', async t => {
  const h = harness(), all = fixture();
  t.mock.method(globalThis, 'fetch', () => { throw Error('must not call model'); });
  const p = await upload(h, all.slice(0, 3));
  const r = await h.send('/v3/recaps', 'POST', recap(p, 2));
  assert.equal(r.status, 422); assert.equal((await r.json()).status, 'upload_required');
  const wrong = recap(p); wrong.cutoff.href = 'OPS/three.xhtml';
  assert.equal((await h.send('/v3/recaps', 'POST', wrong)).status, 422);
});

test('corrupted compressed bytes cannot enter the member cache', async () => {
  const h = harness(), all = fixture().slice(0, 3), p = await plan(h, all);
  const body = Buffer.concat(all.map(m => m.packed)); body[body.length - 2] ^= 1;
  const r = await h.send(`/v3/book/${p.bookKey}/stream/0/${body.length}`, 'PUT', body);
  assert.equal(r.status, 422); assert.equal((await r.json()).status, 'upload_corrupt');
  assert.equal(h.data.has('plan3:reader'), false);
});

test('CRC, declared inflated size and decompression bombs are independently checked', async () => {
  for (const kind of ['crc', 'size', 'bomb']) {
    const h = harness(), members = fixture().slice(0, 3);
    if (kind === 'crc') members[2].entry.crc = (members[2].entry.crc ^ 1) >>> 0;
    else if (kind === 'size') members[2].entry.size++;
    else {
      members[2].packed = deflateRawSync(Buffer.alloc(1048577, 65));
      Object.assign(members[2].entry, {sha: sha(members[2].packed), compressed: members[2].packed.length});
    }
    const p = await plan(h, members), body = Buffer.concat(members.map(m => m.packed));
    const r = await h.send(`/v3/book/${p.bookKey}/stream/0/${body.length}`, 'PUT', body);
    assert.equal(r.status, 422);
  }
});

test('invalid member descriptors, duplicate paths and oversized metadata fail before allocation/upload', async () => {
  for (const patch of [{name: '../outside'}, {sha: 'x'}, {compressed: 1048577}, {size: -1}, {method: 12}]) {
    const h = harness(), entries = fixture().slice(0, 3).map(m => m.entry);
    Object.assign(entries[2], patch);
    assert.equal((await h.send('/v3/plan', 'POST', {entries})).status, 422);
  }
  const h = harness(), entries = fixture().slice(0, 3).map(m => m.entry);
  entries.push(entries[2]); assert.equal((await h.send('/v3/plan', 'POST', {entries})).status, 422);
  assert.equal((await h.send('/v3/plan', 'POST', 'x'.repeat(65537))).status, 422);
});

test('interrupted upload resumes at durable checkpoint after object restart', async () => {
  const h = harness(), members = fixture().slice(0, 3), large = randomBytes(350000).toString('hex');
  const raw = Buffer.from(`<html><body>${large}</body></html>`), packed = deflateRawSync(raw);
  members[2] = {entry: {...members[2].entry, size: raw.length, compressed: packed.length, crc: crc32(raw), sha: sha(packed)}, packed};
  const p = await plan(h, members), body = Buffer.concat(members.map(m => m.packed));
  let step = 0;
  const stream = new ReadableStream({pull(controller) {
    if (step++ === 0) controller.enqueue(body.subarray(0, 180000)); else controller.error(new Error('device disconnected'));
  }});
  let r = await h.send(`/v3/book/${p.bookKey}/stream/0/${body.length}`, 'PUT', stream);
  assert.equal(r.status, 409); h.restart();
  const resumed = await plan(h, members);
  assert.equal(resumed.offset, 131072);
  const rest = body.subarray(resumed.offset);
  r = await h.send(`/v3/book/${p.bookKey}/stream/${resumed.offset}/${rest.length}`, 'PUT', rest);
  assert.equal(r.status, 200, await r.clone().text());
  assert.equal(h.data.get('plan3:reader').offset, body.length);
});

test('base64 desktop transport preserves arbitrary network splits', async () => {
  const h = harness(), members = fixture().slice(0, 3), p = await plan(h, members);
  const body = Buffer.concat(members.map(m => m.packed)), encoded = Buffer.from(body.toString('base64'));
  const stream = new ReadableStream({start(controller) {
    for (let i = 0; i < encoded.length; i += 7) controller.enqueue(encoded.subarray(i, i + 7)); controller.close();
  }});
  const r = await h.send(`/v3/book/${p.bookKey}/stream/0/${body.length}`, 'PUT', stream, 'reader', {'x-recap-encoding': 'base64'});
  assert.equal(r.status, 200); assert.equal(h.data.get('plan3:reader').offset, body.length);
});

test('truncated or oversized segments cannot claim complete upload', async () => {
  for (const delta of [-1, 1]) {
    const h = harness(), members = fixture().slice(0, 3), p = await plan(h, members);
    const body = Buffer.concat(members.map(m => m.packed));
    const r = await h.send(`/v3/book/${p.bookKey}/stream/0/${body.length}`, 'PUT',
      delta < 0 ? body.subarray(0, body.length - 1) : Buffer.concat([body, Buffer.from('x')]));
    assert.notEqual(r.status, 200); assert.equal(h.data.get('plan3:reader').offset, 0);
  }
});

test('incomplete-plan count and expired member storage stay bounded', async () => {
  const h = harness(), members = fixture().slice(0, 3);
  for (let i = 0; i < 12; ++i) await plan(h, members, `reader${i}`);
  assert.equal((await h.storage.list({prefix: 'plan3:'})).size, 8);
  await upload(h, members);
  const books = await h.storage.get('member-books3'); books[0].expires = 0;
  await h.storage.put('member-books3', books);
  // A fresh plan must not claim that expired members are still reusable.
  assert.equal((await plan(h, members, 'fresh')).missing, '111');
  assert.equal((await h.storage.list({prefix: `member3:${books[0].id}:`})).size, 0);
});

test('several large members cross a 1 MiB segment boundary without losing file framing', async () => {
  const h = harness(), members = fixture();
  for (let i = 2; i < members.length; ++i) {
    const raw = Buffer.from(`<html><body>${randomBytes(480000).toString('hex')}</body></html>`);
    const packed = deflateRawSync(raw);
    members[i] = {entry: {...members[i].entry, size: raw.length, compressed: packed.length, crc: crc32(raw), sha: sha(packed)}, packed};
  }
  const p = await upload(h, members);
  assert.ok(p.total > 1048576);
  assert.equal(h.data.get('plan3:reader').offset, p.total);
  assert.equal((await h.storage.list({prefix: 'pending3:'})).size, 0);
  const q = await plan(h, members, 'other');
  assert.equal(q.total, 0); assert.equal(q.missing, '00000');
});

test('a plan whose previously cached member was evicted renegotiates instead of looping forever', async () => {
  const h = harness(), members = fixture().slice(0, 3);
  await upload(h, members);
  const q = await plan(h, members, 'other'); assert.equal(q.total, 0);
  const books = await h.storage.get('member-books3');
  await h.storage.delete(`member-index3:${books[0].id}`);
  const renegotiated = await plan(h, members, 'other');
  assert.equal(renegotiated.missing, '111'); assert.ok(renegotiated.total > 0);
});

test('book-count eviction removes old member chunks and preserves the most recently used book', async () => {
  const h = harness(), first = fixture().slice(0, 3);
  await upload(h, first);
  for (let i = 0; i < 65; ++i) {
    const members = fixture().slice(0, 3);
    const raw = Buffer.from(`<package>edition-${i}</package>`), packed = deflateRawSync(raw);
    members[1] = {entry: {...members[1].entry, size: raw.length, compressed: packed.length, crc: crc32(raw), sha: sha(packed)}, packed};
    await upload(h, members);
  }
  assert.equal((await h.storage.get('member-books3')).length, 64);
  assert.equal((await plan(h, first)).missing, '111');
  assert.equal((await h.storage.get('member-books3')).length, 64);
});

function largePlan(label, count = 5) {
  const members = fixture().slice(0, 2);
  for (let i = 0; i < count; ++i) members.push({entry: {
    name: `OPS/c${i}.xhtml`, method: 0, compressed: 1048000, size: 1048000, crc: 1,
    sha: sha(Buffer.from(`${label}-${i}`))
  }});
  return members;
}

test('overlapping plans cannot reserve more than the per-book budget', async () => {
  const h = harness(), a = largePlan('a'), b = largePlan('b');
  const first = await plan(h, a, 'first');
  const r = await h.send('/v3/plan', 'POST', {entries: b.map(m => m.entry)}, 'second');
  assert.equal(r.status, 429); assert.equal((await r.json()).status, 'busy');
  assert.equal(h.data.get('plan3:first').id, first.bookKey);
  assert.equal(h.data.has('plan3:second'), false);
});

test('identical bytes shared by overlapping plans reserve space only once', async () => {
  const h = harness(), members = largePlan('shared');
  const a = await plan(h, members, 'first'), b = await plan(h, members, 'second');
  assert.ok(a.total > 5 * 1024 * 1024 - 10000);
  assert.equal(b.total, a.total);
  assert.equal((await h.storage.list({prefix: 'plan3:'})).size, 2);
});

test('a fully received plan still reserves unpublished members after a restart', async () => {
  const h = harness(), a = largePlan('a'), b = largePlan('b');
  await plan(h, a, 'first');
  const pending = h.data.get('plan3:first'); pending.offset = pending.total;
  h.restart();
  const r = await h.send('/v3/plan', 'POST', {entries: b.map(m => m.entry)}, 'second');
  assert.equal(r.status, 429); assert.equal((await r.json()).status, 'busy');
});

test('expired reservations release capacity for another device', async () => {
  const h = harness(); await plan(h, largePlan('a'), 'first');
  h.data.get('plan3:first').expires = 0;
  const accepted = await plan(h, largePlan('b'), 'second');
  assert.ok(accepted.total > 0); assert.equal(h.data.has('plan3:first'), false);
});

test('global eviction accounts for outstanding reservations as well as published bytes', async () => {
  const h = harness(), now = Date.now(), cached = fixture().slice(0, 3);
  // Seed a catalogue near its global limit; no large fixture payload allocation
  // is needed to exercise planning/accounting. Existing book entries are pinned
  // by seven outstanding transfers, until the oldest plan is displaced.
  const books = Array.from({length: 7}, (_, i) => ({id: sha(Buffer.from(`cached-${i}`)), bytes: 8 * 1048576,
    expires: now + 86400000, used: i}));
  h.data.set('member-books3', books);
  for (let i = 0; i < books.length; ++i) {
    h.data.set(`member-index3:${books[i].id}`, cached.map(m => m.entry.sha));
    h.data.set(`plan3:seed${i}`, {id: `seed${i}`, bookId: books[i].id, offset: 0, total: 1,
      entries: cached.map(m => m.entry), missing: [2], sealed: [], expires: now + 10000 + i});
  }
  const a = largePlan('global-a', 7), b = largePlan('global-b', 7);
  // Give the two incoming plans distinct package identities.
  b[1].entry = {...b[1].entry, sha: sha(Buffer.from('other-package'))};
  await plan(h, a, 'first'); await plan(h, b, 'second');
  const bytes = (await h.storage.get('member-books3')).reduce((n, book) => n + book.bytes, 0);
  const incoming = h.data.get('plan3:first').total + h.data.get('plan3:second').total;
  assert.ok(bytes + incoming <= 64 * 1048576);
  assert.ok(bytes < 56 * 1048576, 'must evict cached bytes, not promise the same free space twice');
});

test('final publication guard rejects a legacy overcommitted plan without exceeding the cache', async () => {
  const h = harness(), members = fixture().slice(0, 3), p = await plan(h, members);
  const books = await h.storage.get('member-books3'); books[0].bytes = 8 * 1048576;
  await h.storage.put('member-books3', books);
  const body = Buffer.concat(members.map(m => m.packed));
  const r = await h.send(`/v3/book/${p.bookKey}/stream/0/${body.length}`, 'PUT', body);
  assert.equal(r.status, 429); assert.equal((await r.json()).status, 'busy');
  assert.equal(h.data.has('plan3:reader'), false);
  assert.equal((await h.storage.get('member-books3'))[0].bytes, 8 * 1048576);
  assert.equal((await h.storage.list({prefix: 'member3:'})).size, 0);
});
