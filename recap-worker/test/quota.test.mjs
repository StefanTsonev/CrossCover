import test from 'node:test';
import assert from 'node:assert/strict';
import {quotaDetails, quotaReply} from '../src/quota.js';
const now = 1800000000000;
const failure = ids => ({'@type': 'type.googleapis.com/google.rpc.QuotaFailure', violations: ids.map(quotaId => ({quotaId}))});
const retry = retryDelay => ({'@type': 'type.googleapis.com/google.rpc.RetryInfo', retryDelay});
const response = (details = [], headers = {}) => new Response(JSON.stringify({error: {details}}), {status: 429, headers});

test('structured RPM and TPM violations are identified as minute quotas', async () => {
  for (const id of ['GenerateRequestsPerMinutePerProjectPerModel-FreeTier', 'GenerateContentInputTokensPerModelPerMinute-FreeTier'])
    assert.equal((await quotaDetails(response([failure([id])]), now)).kind, 'minute');
});
test('daily quota takes priority over a concurrent minute violation and short retry hint', async () => {
  const info = await quotaDetails(response([failure(['GenerateRequestsPerMinutePerProjectPerModel-FreeTier',
    'GenerateRequestsPerDayPerProjectPerModel-FreeTier']), retry('12.1s')]), now);
  assert.deepEqual(info, {kind: 'daily', retryAt: now + 13000});
});
test('a RetryInfo delay alone or a plain error message never implies a minute quota', async () => {
  assert.deepEqual(await quotaDetails(response([retry('42.3s')]), now), {kind: 'unknown', retryAt: now + 43000});
  const r = new Response(JSON.stringify({error: {message: 'per day quota reached'}}), {status: 429});
  assert.deepEqual(await quotaDetails(r, now), {kind: 'unknown', retryAt: null});
});
test('mixed known and unknown violations keep the generic error', async () => {
  assert.equal((await quotaDetails(response([failure(['GenerateRequestsPerMinute', 'BillingRestriction'])]), now)).kind, 'unknown');
});
test('malformed violation elements fall back to the generic quota message', async () => {
  const info = await quotaDetails(response([{'@type': 'type.googleapis.com/google.rpc.QuotaFailure',
    violations: [null, 7, 'unexpected']}, retry('15s')]), now);
  assert.deepEqual(info, {kind: 'unknown', retryAt: now + 15000});
});
test('Retry-After seconds/date and protobuf durations use the longest explicit wait', async () => {
  const date = new Date(now + 90000).toUTCString();
  assert.equal((await quotaDetails(response([retry('12s')], {'retry-after': date}), now)).retryAt, now + 90000);
  assert.equal((await quotaDetails(response([retry('90s')], {'retry-after': '45'}), now)).retryAt, now + 90000);
});
test('malformed and oversized provider errors retain valid Retry-After without exposing their text', async () => {
  for (const body of ['not-json', 'x'.repeat(16385)]) {
    const info = await quotaDetails(new Response(body, {status: 429, headers: {'retry-after': '60'}}), now);
    assert.deepEqual(info, {kind: 'unknown', retryAt: now + 60000});
  }
});
test('invalid, negative, zero and unreasonably large delays never become user-facing retry times', async () => {
  for (const delay of ['-5s', '0s', 'NaNs', 'Infinitys', '999999999999s', 50])
    assert.equal((await quotaDetails(response([retry(delay)]), now)).retryAt, null);
});
test('cooldown replies preserve type and decrease the remaining provider wait', async () => {
  const r = quotaReply({kind: 'minute', retryAt: now + 90500}, now + 30000);
  assert.equal(r.status, 429); assert.equal(r.headers.get('retry-after'), '61');
  assert.deepEqual(await r.json(), {status: 'budget_exhausted', quota: 'minute', retryAfter: 61});
});
test('internal fallback cooldown is never presented as a provider reset time', async () => {
  const r = quotaReply({kind: 'unknown', retryAt: null}, now);
  assert.equal(r.headers.get('retry-after'), null);
  assert.deepEqual(await r.json(), {status: 'budget_exhausted', quota: 'unknown'});
});
