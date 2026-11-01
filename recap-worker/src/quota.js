import {readBounded, reply} from './core.js';

// Classify structured quota identifiers only. 429 alone also covers other
// provider restrictions, and a RetryInfo delay does not identify the quota.
export async function quotaDetails(response, now = Date.now()) {
  let details = [];
  try {
    const body = JSON.parse(await readBounded(response, 16384));
    if (Array.isArray(body.error?.details)) details = body.error.details;
  } catch { /* Keep the generic quota message for malformed/oversized errors. */ }
  const ids = details.filter(d => d?.['@type'] === 'type.googleapis.com/google.rpc.QuotaFailure')
    .flatMap(d => Array.isArray(d.violations) ? d.violations : [])
    .map(v => String(v?.quotaId || v?.quotaMetric || '').toLowerCase());
  const day = s => /per[_/-]?day/.test(s);
  const minute = s => /per[_/-]?minute/.test(s);
  const kind = ids.some(day) ? 'daily' : ids.length && ids.every(minute) ? 'minute' : 'unknown';
  const delays = [];
  const header = response.headers.get('retry-after');
  if (header !== null) {
    const value = /^\d+(?:\.\d+)?$/.test(header.trim()) ? Number(header) : (Date.parse(header) - now) / 1000;
    if (Number.isFinite(value) && value > 0 && value <= 604800) delays.push(Math.ceil(value));
  }
  for (const detail of details) {
    if (detail?.['@type'] !== 'type.googleapis.com/google.rpc.RetryInfo' || typeof detail.retryDelay !== 'string') continue;
    if (!/^\d+(?:\.\d{1,9})?s$/.test(detail.retryDelay)) continue;
    const seconds = Number(detail.retryDelay.slice(0, -1));
    if (seconds > 0 && seconds <= 604800) delays.push(Math.ceil(seconds));
  }
  return {kind, retryAt: delays.length ? now + Math.max(...delays) * 1000 : null};
}

export function quotaReply(info, now = Date.now()) {
  const retryAfter = info?.retryAt > now ? Math.ceil((info.retryAt - now) / 1000) : null;
  return reply({status: 'budget_exhausted', quota: info?.kind || 'unknown',
    ...(retryAfter ? {retryAfter} : {})}, 429, retryAfter ? {'retry-after': String(retryAfter)} : {});
}
