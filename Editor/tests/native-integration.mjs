import {_electron as electron,expect} from '@playwright/test';
import {spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {randomUUID} from 'node:crypto';
import assert from 'node:assert/strict';
import {performance} from 'node:perf_hooks';
const editor=path.resolve(import.meta.dirname,'..'),native=path.resolve(editor,'../bin/Release/x64/HidHideClient.exe');
const root=path.join(os.tmpdir(),'HidHide-Profiles-Restart-Test-'+randomUUID());
const output=path.resolve(editor,'../artifacts/electron-ui-validation');
const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
const fixtureStarted=performance.now();
const engine=spawn(native,['--profiles-editor-host',root],{windowsHide:true,stdio:'ignore'});
let app;const checks=[],startupMs=[];let coldFixtureToReadyMs;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function until(check,message){for(let i=0;i<100;i++){if(await check())return;await sleep(100);}throw Error(message);}
function exchange(value){return new Promise((resolve,reject)=>{const p=spawn(native,['--editor-fixture-request',root],{windowsHide:true,stdio:['pipe','pipe','pipe']});let out='',err='';const timer=setTimeout(()=>{p.kill();reject(Error('Fixture bridge timed out'));},15000);p.stdout.setEncoding('utf8');p.stderr.setEncoding('utf8');p.stdout.on('data',s=>out+=s);p.stderr.on('data',s=>err+=s);p.on('error',reject);p.on('close',()=>{clearTimeout(timer);try{const result=JSON.parse(out);if(!result.ok)throw Error(result.error);resolve(result);}catch(e){reject(Error(e.message+' '+err));}});p.stdin.end(JSON.stringify(value));});}
async function open(){const started=performance.now();app=await electron.launch({args:[editor,'--fixture-native',root],env});const page=await app.firstWindow();await expect(page.getByText('Profile engine running',{exact:true})).toBeVisible({timeout:20000});await expect(page.getByRole('heading',{name:'Fixture Game',exact:true})).toBeVisible();startupMs.push(performance.now()-started);return page;}
async function close(page){const metrics=await app.evaluate(({app})=>app.getAppMetrics());const bridgePid=await app.evaluate(()=>globalThis.__nativeBridgePid?.());assert(bridgePid>0,'Persistent native bridge did not start');const child=app.process();const ended=new Promise(resolve=>child.once('exit',resolve));await page.getByRole('button',{name:'Close editor',exact:true}).click().catch(e=>{if(!e.message.includes('closed'))throw e;});await ended;await until(()=>metrics.every(m=>{try{process.kill(m.pid,0);return false;}catch{return true;}}),'Frontend subprocesses remained after close');await until(()=>{try{process.kill(bridgePid,0);return false;}catch{return true;}},'Persistent native bridge remained after editor close');app=null;return metrics;}
try{
 await fs.mkdir(output,{recursive:true});
 await until(()=>fs.access(path.join(root,'editor-host-ready.json')).then(()=>true,()=>false),'Native fixture did not start');
 let response=await exchange({command:'fixture-process',running:true});
 let page=await open();
 coldFixtureToReadyMs=performance.now()-fixtureStarted;
 await expect(page.getByRole('heading',{name:'Fixture Game',exact:true})).toBeVisible({timeout:20000});
 const wheel=page.getByRole('row').filter({has:page.getByText('Fixture steering wheel',{exact:true})});
 await wheel.getByLabel('Hidden',{exact:true}).check();await page.getByRole('button',{name:'Apply changes',exact:true}).click();
 await expect(page.getByText('No pending changes',{exact:true})).toBeVisible({timeout:20000});
 await expect(wheel.getByRole('cell').nth(1)).toContainText('Hidden');
 await page.screenshot({path:path.join(output,'native-connected.png')});
 checks.push('Electron edits persisted through production native repository CAS and verified enforcement adapter seam');
 const before=await exchange({command:'fixture-state'});assert.equal(before.editorOpen,true);
 const metrics=await close(page);await until(async()=>!(await exchange({command:'fixture-state'})).editorOpen,'Editor registration outlived process');
 assert.equal(engine.exitCode,null);response=await exchange({command:'snapshot'});assert.equal(response.snapshot.verified,true);assert(response.snapshot.devices.some(d=>d.name==='Fixture steering wheel'&&d.current==='Hidden'));
 checks.push('All Electron processes exited; native engine and applied policy survived');
 await exchange({command:'fixture-process',running:false});await until(async()=>{const s=(await exchange({command:'snapshot'})).snapshot;return s.profiles.find(p=>p.id===s.activeId)?.name==='Default';},'Global fallback did not switch with editor closed');
 await exchange({command:'fixture-process',running:true});await until(async()=>{const s=(await exchange({command:'snapshot'})).snapshot;return s.profiles.find(p=>p.id===s.activeId)?.name==='Fixture Game';},'Application profile did not switch with editor closed');
 checks.push('Automatic application and Global switching continue with no frontend');
 page=await open();await expect(page.getByRole('heading',{name:'Fixture Game',exact:true})).toBeVisible({timeout:20000});
 await expect(page.getByRole('row').filter({has:page.getByText('Fixture steering wheel',{exact:true})}).getByRole('cell').nth(1)).toContainText('Hidden');
 checks.push('Fresh editor reconnected to saved and observed current policy');
 for(let repeat=0;repeat<3;repeat++){
  await close(page);page=await open();
  await expect(page.getByRole('row').filter({has:page.getByText('Fixture steering wheel',{exact:true})}).getByRole('cell').nth(1)).toContainText('Hidden');
 }
 await exchange({command:'fixture-stop'});await expect(page.getByText('Profile engine unavailable',{exact:true})).toBeVisible({timeout:20000});
 await expect(page.getByText('Engine connection unavailable. Applied state is unknown.')).toBeVisible();
 await close(page);const stopped=JSON.parse(await fs.readFile(path.join(root,'editor-host-stopped.json'),'utf8'));assert.equal(stopped.baselineRestored,true);
 checks.push('Engine shutdown restores fixture baseline and makes editor observation explicitly unknown');
 await fs.writeFile(path.join(output,'native-integration.json'),JSON.stringify({checks,fixtureRoot:root,startupMs,coldFixtureToReadyMs,metricsWhileOpen:metrics,allFrontendProcessesExited:true,realDriverUsed:false},null,2));console.log(`${checks.length} native/Electron integration groups passed. Editor launch-to-ready: ${startupMs.map(x=>x.toFixed(1)).join(', ')} ms.`);
}finally{if(app)await app.close().catch(()=>{});if(engine.exitCode===null)await exchange({command:'fixture-stop'}).catch(()=>{});}
