import assert from 'node:assert/strict';
import { readFile, access } from 'node:fs/promises';
import { test } from 'node:test';
import { createHash } from 'node:crypto';

const root = new URL('../extension/', import.meta.url);
const manifest = JSON.parse(await readFile(new URL('manifest.json', root), 'utf8'));

test('Chrome can load a Manifest V3 worker with packaged entry points', async () => {
  assert.equal(manifest.manifest_version, 3);
  assert.equal(manifest.background.type, 'module');
  assert.equal(manifest.background.persistent, undefined);
  assert.ok(!manifest.permissions.includes('webRequestBlocking'));
  assert.ok(!manifest.permissions.includes('<all_urls>'));
  for (const path of [manifest.background.service_worker, manifest.action.default_popup,
    ...manifest.content_scripts.flatMap(script => script.js)]) {
    await access(new URL(path, root));
  }
});

test('migration preserves the native messaging extension identity', () => {
  const hash = createHash('sha256').update(Buffer.from(manifest.key, 'base64')).digest('hex').slice(0, 32);
  const id = [...hash].map(digit => String.fromCharCode(97 + parseInt(digit, 16))).join('');
  assert.equal(id, 'knldjmfmopnpolahpmmgbagdohdnhkik');
});
