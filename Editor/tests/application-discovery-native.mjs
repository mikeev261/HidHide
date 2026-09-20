// Harmless real processes only; the helper must work without an installed coordinator/driver.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import path from 'node:path';
import {createRequire} from 'node:module';
import {_electron as electron,expect} from '@playwright/test';
const {createApplicationDiscovery}=createRequire(import.meta.url)('../electron/application-discovery.cjs');
const native=path.resolve(import.meta.dirname,'../../bin/Release/x64/HidHideClient.exe');
const discovery=createApplicationDiscovery(native);
const executable=process.execPath;
const children=[spawn(executable,['-e','setInterval(()=>{},1000)'],{windowsHide:true,stdio:'ignore'}),spawn(executable,['-e','setInterval(()=>{},1000)'],{windowsHide:true,stdio:'ignore'})];
try{
 await Promise.all(children.map(child=>new Promise((resolve,reject)=>{child.once('spawn',resolve);child.once('error',reject);} )));
 const list=await discovery.run('list');
 const matching=list.applications.filter(a=>a.path.toLowerCase()===executable.toLowerCase());
 assert.equal(matching.length,1);assert.ok(matching[0].instances>=2);assert.ok(matching[0].created);assert.ok(matching[0].name);
 const selected=matching[0];await discovery.run('validate',selected);
 await assert.rejects(discovery.run('validate',{...selected,created:String(BigInt(selected.created)+1n)}),/exited or changed/);
 await assert.rejects(discovery.run('validate',{...selected,path:native}),/exited or changed/);
 const metadata=await discovery.run('describe',{path:native});assert.equal(metadata.application.name,'HidHide Profiles');
 // Get a precise identity for a uniquely named child by copying Node within the ignored fixture directory.
 const fs=await import('node:fs/promises');const directory=path.resolve(import.meta.dirname,'../../artifacts/discovery-fixture');await fs.mkdir(directory,{recursive:true});
 const childPath=path.join(directory,'discovery-sleeper.exe');await fs.copyFile(executable,childPath);
 const child=spawn(childPath,['-e','setInterval(()=>{},1000)'],{windowsHide:true,stdio:'ignore'});
 try{
  await new Promise((resolve,reject)=>{child.once('spawn',resolve);child.once('error',reject);});
  const row=(await discovery.run('list')).applications.find(a=>a.pid===child.pid);assert.ok(row);
  await discovery.run('validate',row);
  const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
  const editor=await electron.launch({args:[path.resolve(import.meta.dirname,'..'),'--fixture','--fixture-discovery-native'],env});
  try{
   const page=await editor.firstWindow();await expect(page.getByRole('heading',{name:'Le Mans Ultimate',exact:true})).toBeVisible();
   await page.getByRole('button',{name:'New profile',exact:true}).click();
   await page.getByRole('button',{name:'Choose running application',exact:true}).click();
   await page.getByLabel('Find running application').fill(childPath);
   const target=page.locator('.running-applications button');await expect(target).toHaveCount(1);await target.click();
   await expect(page.getByLabel('Profile name',{exact:true})).not.toHaveValue('');
   await page.getByLabel('Profile name',{exact:true}).fill('Native discovery fixture');
   await page.getByRole('button',{name:'Create draft',exact:true}).click();
   await expect(page.getByRole('heading',{name:'Native discovery fixture',exact:true})).toBeVisible();
   const state=await editor.evaluate(()=>globalThis.__fixtureControl({}));assert.ok(!state.profiles.some(p=>p.name==='Native discovery fixture'));
  }finally{await editor.evaluate(({app})=>app.exit(0)).catch(()=>{});await editor.close().catch(()=>{});}
  const exited=new Promise(resolve=>child.once('exit',resolve));child.kill();await exited;
  await assert.rejects(discovery.run('validate',row),/exited or changed/);
 }finally{child.kill();}
 console.log('PASS native discovery: real process grouping, metadata, exact path/lifetime checks, exited process, Electron/preload/native picker to detached draft with fixture-only policy engine.');
}finally{discovery.cancel();for(const child of children)child.kill();}
