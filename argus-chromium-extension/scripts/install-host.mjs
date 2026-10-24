#!/usr/bin/env node
import { access, mkdir, readFile, realpath, writeFile } from 'node:fs/promises';
import { constants } from 'node:fs';
import { homedir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// Exported for validation without changing the user's browser registration.
export async function installHost(helperPath, browser = 'chrome', destination) {
  const directories = { chrome: 'Google/Chrome', edge: 'Microsoft Edge' };
  if (!directories[browser]) throw new Error('Browser must be chrome or edge');
  const executable = await realpath(resolve(helperPath));
  await access(executable, constants.X_OK);
  const template = JSON.parse(await readFile(new URL('../../mainfest/com.argus.extension.helper.json', import.meta.url), 'utf8'));
  const directory = destination || join(homedir(), 'Library/Application Support', directories[browser], 'NativeMessagingHosts');
  const path = join(directory, `${template.name}.json`);
  await mkdir(directory, { recursive: true });
  await writeFile(path, JSON.stringify({ ...template, path: executable }, null, 2) + '\n');
  return path;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [helperPath, browser = 'chrome'] = process.argv.slice(2);
  if (!helperPath || process.platform !== 'darwin') {
    console.error('Usage (macOS): node scripts/install-host.mjs /absolute/path/to/ArgusMacExtensionHelper [chrome|edge]');
    process.exitCode = 1;
  } else {
    try { console.log(await installHost(helperPath, browser)); }
    catch (error) { console.error(error.message); process.exitCode = 1; }
  }
}
