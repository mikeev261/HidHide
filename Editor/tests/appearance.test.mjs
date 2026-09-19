import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
const {createAppearanceStore}=createRequire(import.meta.url)('../electron/appearance.cjs');
test('appearance survives a new process store, orders writes and preserves last saved state on failure',async()=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'HidHide-Appearance-')),file=path.join(dir,'editor-appearance.json');
 const store=createAppearanceStore(dir);assert.equal(await store.load(),'dark');
 await Promise.all([store.set('light'),store.set('dark'),store.set('light')]);
 assert.equal(await createAppearanceStore(dir).load(),'light');
 const before=await fs.readFile(file,'utf8');await assert.rejects(store.set('invalid'));assert.equal(await fs.readFile(file,'utf8'),before);
 await fs.mkdir(file+'.tmp');await assert.rejects(store.set('dark'));assert.equal(store.get(),'light');assert.equal(await fs.readFile(file,'utf8'),before);
 await fs.rmdir(file+'.tmp');await store.set('dark');assert.equal(await createAppearanceStore(dir).load(),'dark');
 await fs.writeFile(file,'malformed');assert.equal(await createAppearanceStore(dir).load(),'dark');
});
