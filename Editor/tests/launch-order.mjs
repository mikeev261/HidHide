import assert from 'node:assert/strict';
import {spawn,execFileSync} from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {randomUUID} from 'node:crypto';
import {_electron as electron} from '@playwright/test';

const editor=path.resolve(import.meta.dirname,'..');
const repository=path.resolve(editor,'..');
const native=path.join(repository,'bin/Release/x64/HidHideClient.exe');
const root=path.join(os.tmpdir(),'HidHide-Profiles-Restart-Test-'+randomUUID());
const output=path.join(repository,'artifacts/launch-order');
const probe=path.join(output,'probe-build');
await fs.mkdir(output,{recursive:true});
execFileSync('dotnet',['publish',path.join(editor,'tests/LaunchProbe/LaunchProbe.csproj'),'-c','Release','-o',probe],{stdio:'inherit'});
const env={...process.env,HIDHIDE_LAUNCH_PROBE_ROOT:root};
const engine=spawn(native,['--profiles-editor-host',root],{windowsHide:true,stdio:'ignore',env});
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const exists=file=>fs.access(file).then(()=>true,()=>false);
async function until(check,message){for(let i=0;i<100;i++){if(await check())return;await delay(100);}throw Error(message);}
function request(value){return new Promise((resolve,reject)=>{
 const p=spawn(native,['--editor-fixture-request',root],{windowsHide:true,stdio:['pipe','pipe','pipe'],env});let out='',err='';
 const timer=setTimeout(()=>{p.kill();reject(Error('Fixture bridge timed out'));},15000);
 p.stdout.setEncoding('utf8');p.stderr.setEncoding('utf8');p.stdout.on('data',s=>out+=s);p.stderr.on('data',s=>err+=s);
 p.on('error',reject);p.on('close',()=>{clearTimeout(timer);try{resolve(JSON.parse(out));}catch(e){reject(Error(String(e)+' '+err));}});
 p.stdin.end(JSON.stringify(value));
});}
async function ok(value){const reply=await request(value);assert.equal(reply.ok,true,reply.error);return reply;}
async function blocked(value,pattern){const reply=await request(value);assert.equal(reply.ok,false,'Operation should have been blocked');assert.match(reply.error,pattern);}
function launchResources(){
 return JSON.parse(execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',
  `@{handles=(Get-Process -Id ${engine.pid}).HandleCount;children=@(Get-CimInstance Win32_Process -Filter 'ParentProcessId = ${engine.pid}' | Select-Object -ExpandProperty Name)} | ConvertTo-Json -Compress`],{encoding:'utf8',windowsHide:true}));
}
let checks=[];
let app;
try{
 await until(()=>exists(path.join(root,'editor-host-ready.json')),'Native fixture did not start');
 for(const file of ['LaunchProbe.exe','LaunchProbe.dll','LaunchProbe.runtimeconfig.json'])
  await fs.copyFile(path.join(probe,file),path.join(root,file==='LaunchProbe.exe'?'FixtureGame.exe':file));
 const initial=(await ok({command:'snapshot'})).snapshot;
 const game=initial.profiles.find(p=>p.name==='Fixture Game');assert(game?.version);
 const command=(snapshot=initial)=>({command:'launch',id:game.id,expected:snapshot.profiles.find(p=>p.id===game.id).version,expectedSettings:snapshot.settingsVersion});
 await blocked({...command(),expected:{...game.version,hash:'0'.repeat(64)}},/changed|refresh/i);
 assert.equal(await exists(path.join(root,'launch-probe-started.json')),false);
 checks.push('Stale profile hash blocks launch before creating a child');

 await ok({command:'fixture-process',running:true});
 await blocked(command((await ok({command:'snapshot'})).snapshot),/already running|close it/i);
 await ok({command:'fixture-process',running:false});
 checks.push('An existing matched process is rejected because its old handles cannot be repaired');

 let snapshot=(await ok({command:'snapshot'})).snapshot;
 await ok({command:'settings',settings:{...snapshot.settings,allowedApplications:[...snapshot.settings.allowedApplications,path.join(root,'FixtureGame.exe')]},expectedSettings:snapshot.settingsVersion});
 snapshot=(await ok({command:'snapshot'})).snapshot;
 await blocked(command(snapshot),/Allowed apps/i);
 await ok({command:'settings',settings:{...snapshot.settings,allowedApplications:[]},expectedSettings:snapshot.settingsVersion});
 checks.push('A global Allowed-app exemption blocks a supposedly hidden launch');

 // A readable .exe with invalid executable contents passes metadata validation.
 // CreateProcess itself must succeed before Launch is allowed to change policy.
 await fs.writeFile(path.join(root,'Bad.exe'),'This is not a Windows executable.');
 const bad=(await ok({command:'new',kind:'application',name:'Bad executable',executable:path.join(root,'Bad.exe')})).profile;
 snapshot=(await ok({command:'snapshot'})).snapshot;
 await ok({command:'apply',profile:bad,expected:null,settings:{...snapshot.settings,mode:'useGlobal'},expectedSettings:snapshot.settingsVersion});
 await ok({command:'fixture-process',running:true});
 snapshot=(await ok({command:'snapshot'})).snapshot;
 assert.equal(snapshot.activeId,initial.settings.selectedGlobalId);assert(snapshot.runningOrder.includes(game.id));
 const beforeBad=await fs.readFile(path.join(root,'Profiles/settings.json'));
 const beforeWrites=(await ok({command:'fixture-state'})).enforcementWrites;
 const resourcesBefore=launchResources();
 for(let attempt=0;attempt<8;attempt++)
  await blocked({command:'launch',id:bad.id,expected:snapshot.profiles.find(p=>p.id===bad.id).version,expectedSettings:snapshot.settingsVersion},/Create suspended application/i);
 const afterBad=(await ok({command:'snapshot'})).snapshot;
 assert.deepEqual(afterBad.settingsVersion,snapshot.settingsVersion);
 assert.deepEqual(await fs.readFile(path.join(root,'Profiles/settings.json')),beforeBad);
 assert.equal(afterBad.activeId,snapshot.activeId);assert.equal(afterBad.verified,true);assert.equal(afterBad.settings.mode,'useGlobal');
 assert.equal((await ok({command:'fixture-state'})).enforcementWrites,beforeWrites);
 const resourcesAfter=launchResources();
 assert.deepEqual(resourcesAfter.children,resourcesBefore.children,'Failed creation leaked a child');
 assert(resourcesAfter.handles<=resourcesBefore.handles+2,'Repeated failed creation leaked handles');
 checks.push('Repeated invalid-EXE creation preserves Use Global settings bytes/hash and verified mask despite another running profile, without leaked children or handles');
 await ok({command:'fixture-process',running:false});

 snapshot=(await ok({command:'snapshot'})).snapshot;
 await ok({command:'fixture-launch-readback',fail:true});
 await ok({command:'fixture-launch-witness'});
 await blocked(command(snapshot),/applied and verified|readback/i);
 assert.equal(await exists(path.join(root,'launch-probe-started.json')),false);
 await ok({command:'fixture-launch-readback',fail:false});
 await ok({command:'retry'});
 snapshot=(await ok({command:'snapshot'})).snapshot;
 assert.equal(snapshot.settings.mode,'useGlobal');assert.equal(snapshot.activeId,initial.settings.selectedGlobalId);assert.equal(snapshot.verified,true);
 assert.deepEqual(launchResources().children,resourcesBefore.children,'Readback failure leaked a suspended child');
 checks.push('Unknown driver readback aborts the suspended child before its first instruction and restores the previous saved Use Global mode');

 snapshot=(await ok({command:'snapshot'})).snapshot;
 await ok({command:'settings',settings:{...snapshot.settings,mode:'useGlobal'},expectedSettings:snapshot.settingsVersion});
 snapshot=(await ok({command:'snapshot'})).snapshot;
 await ok({command:'fixture-launch-witness'});
 const launching=request(command(snapshot));
 await until(()=>exists(path.join(root,'launch-readback-started.flag')),'Launch did not reach readback');
 assert.equal(await exists(path.join(root,'launch-probe-started.json')),false,'Child executed during delayed driver readback');
 const launched=await launching;assert.equal(launched.ok,true,launched.error);
 await until(()=>exists(path.join(root,'launch-probe-started.json')),'Launched probe did not execute');
 const witness=JSON.parse(await fs.readFile(path.join(root,'launch-probe-started.json'),'utf8'));
 assert.equal(witness.verifiedAtManagedEntry,true);
 checks.push('A real Windows child remained suspended through delayed readback and saw verified policy at managed entry');

 await delay(700);
 snapshot=(await ok({command:'snapshot'})).snapshot;
 assert.equal(snapshot.settings.mode,'automatic');assert.equal(snapshot.launchedId,'');assert.equal(snapshot.activeId,game.id);assert.equal(snapshot.verified,true);
 await ok({command:'settings',settings:{...snapshot.settings,startWithWindows:false},expectedSettings:snapshot.settingsVersion});
 await ok({command:'fixture-process',running:false});
 snapshot=(await ok({command:'snapshot'})).snapshot;assert.equal(snapshot.activeId,game.id);
 checks.push('Owned process identity participates in normal selection and saved settings remain editable');
 const newer=(await ok({command:'new',kind:'application',name:'Newer application',executable:path.join(root,'Newer.exe')})).profile;
 snapshot=(await ok({command:'snapshot'})).snapshot;
 await ok({command:'apply',profile:newer,expected:null,settings:snapshot.settings,expectedSettings:snapshot.settingsVersion});
 await ok({command:'fixture-processes',processes:[{id:newer.id,pid:60002,created:2}],incomplete:false});
 snapshot=(await ok({command:'snapshot'})).snapshot;assert.equal(snapshot.activeId,newer.id);
 await ok({command:'fixture-processes',processes:[],incomplete:false});
 snapshot=(await ok({command:'snapshot'})).snapshot;assert.equal(snapshot.activeId,game.id);
 checks.push('A newer activation replaces the launched application mask and its exit restores the still-running launched application');
 await fs.copyFile(path.join(probe,'LaunchProbe.exe'),path.join(root,'Newer.exe'));
 snapshot=(await ok({command:'snapshot'})).snapshot;
 await ok({command:'launch',id:newer.id,expected:snapshot.profiles.find(p=>p.id===newer.id).version,expectedSettings:snapshot.settingsVersion});
 snapshot=(await ok({command:'snapshot'})).snapshot;assert.equal(snapshot.activeId,newer.id);assert.equal(snapshot.launchedId,'');
 assert(snapshot.runningOrder.includes(game.id));
 checks.push('A second direct launch is an ordinary newer activation while the first launched application remains running');

 await fs.writeFile(path.join(root,'launch-probe-stop.flag'),'stop');
 await until(async()=>{const next=(await ok({command:'snapshot'})).snapshot;return !next.launchedId && next.activeId===initial.settings.selectedGlobalId;},'Global fallback did not resume after child exit');
 checks.push('After the exact child exits, a fresh scan restores the selected Global policy');

 await fs.rm(path.join(root,'launch-probe-started.json'));
 await fs.rm(path.join(root,'launch-probe-stop.flag'));
 await ok({command:'fixture-launch-witness'});
 const electronEnv={...env};delete electronEnv.ELECTRON_RUN_AS_NODE;
 app=await electron.launch({args:[editor,'--fixture-native',root],env:electronEnv});
 const page=await app.firstWindow();
 await page.getByRole('heading',{name:'Default'}).waitFor({timeout:20000});
 await page.locator('.profile-item').filter({hasText:'Fixture Game'}).click();
 await page.getByRole('button',{name:'Launch with profile'}).click();
 await until(()=>exists(path.join(root,'launch-probe-started.json')),'Editor launch did not reach the native child');
 assert.equal(JSON.parse(await fs.readFile(path.join(root,'launch-probe-started.json'),'utf8')).verifiedAtManagedEntry,true);
 snapshot=(await ok({command:'snapshot'})).snapshot;assert.equal(snapshot.launchedId,'');assert.equal(snapshot.activeId,game.id);
 checks.push('The actual Electron button passed the command allowlist and authenticated native bridge to launch the verified child');
 await fs.writeFile(path.join(root,'launch-probe-stop.flag'),'stop');
 await until(async()=>(await ok({command:'snapshot'})).snapshot.activeId===initial.settings.selectedGlobalId,'Editor-launched child did not exit');
 await app.close();app=null;
 await ok({command:'fixture-stop'});
 await until(()=>exists(path.join(root,'editor-host-stopped.json')),'Native fixture did not stop');
 await fs.writeFile(path.join(output,'result.json'),JSON.stringify({checks,realDriverUsed:false,root},null,2));
 console.log(`${checks.length} launch-order fixture groups passed.`);
}finally{
 if(app)await app.close().catch(()=>{});
 if(engine.exitCode===null){await fs.writeFile(path.join(root,'launch-probe-stop.flag'),'stop').catch(()=>{});await request({command:'fixture-stop'}).catch(()=>{});}
}
