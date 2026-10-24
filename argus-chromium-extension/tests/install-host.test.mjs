import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { installHost } from '../scripts/install-host.mjs';

test('writes a host registration with an executable absolute path and existing origin', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'argus-host-test-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const path = await installHost(process.execPath, 'chrome', directory);
  const host = JSON.parse(await readFile(path, 'utf8'));
  assert.equal(host.name, 'com.argus.extension.helper');
  assert.equal(host.type, 'stdio');
  assert.equal(host.path, process.execPath);
  assert.deepEqual(host.allowed_origins, ['chrome-extension://knldjmfmopnpolahpmmgbagdohdnhkik/']);
});

test('rejects an unavailable helper or unsupported browser instead of creating a broken registration', async () => {
  await assert.rejects(installHost('/missing/ArgusMacExtensionHelper'), /ENOENT/);
  await assert.rejects(installHost(process.execPath, 'safari'), /chrome or edge/);
});
