import assert from 'node:assert/strict';
import {execFile,spawn} from 'node:child_process';
import {promisify} from 'node:util';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {randomUUID} from 'node:crypto';
import {_electron as electron,expect} from '@playwright/test';
const editor=path.resolve(import.meta.dirname,'..'),repository=path.resolve(editor,'..');
const native=path.join(repository,'bin/Release/x64/HidHideClient.exe');
const root=path.join(os.tmpdir(),'HidHide-Profiles-Restart-Test-'+randomUUID());
const output=path.join(repository,'artifacts/recent-mask');
const engine=spawn(native,['--profiles-editor-host',root],{windowsHide:true,stdio:'ignore'});
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function until(check,message){for(let i=0;i<100;i++){if(await check())return;await delay(100);}throw Error(message);}
const exited=child=>child.exitCode!==null||child.signalCode!==null;
async function stopFixtureTree(child){
 if(exited(child))return;
 await promisify(execFile)('taskkill',['/PID',String(child.pid),'/T','/F'],{windowsHide:true,timeout:5000}).catch(error=>{if(!exited(child))throw error;});
 await until(()=>exited(child),'Fixture process did not exit after cleanup');
}
function request(value){return new Promise((resolve,reject)=>{
 const child=spawn(native,['--editor-fixture-request',root],{windowsHide:true,stdio:['pipe','pipe','pipe']});let out='',err='';
 const timer=setTimeout(()=>{child.kill();reject(Error('Fixture bridge timeout'));},20000);
 child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');child.stdout.on('data',s=>out+=s);child.stderr.on('data',s=>err+=s);child.on('error',reject);
 child.on('close',()=>{clearTimeout(timer);try{resolve(JSON.parse(out));}catch(e){reject(Error(String(e)+err));}});child.stdin.end(JSON.stringify(value));
});}
async function ok(value){const r=await request(value);assert.equal(r.ok,true,r.error);return r;}
const snapshot=async()=>(await ok({command:'snapshot'})).snapshot;
const sample=(processes,incomplete=false)=>({processes,incomplete});
const scan=async(processes,incomplete=false)=>(await ok({command:'fixture-processes',...sample(processes,incomplete)})).snapshot;
async function pin(id){const s=await snapshot();return ok({command:id?'mask':'automatic',id,expected:s.profiles.find(p=>p.id===id)?.version,expectedSettings:s.settingsVersion});}
let app;const checks=[];
try{
 await until(()=>fs.access(path.join(root,'editor-host-ready.json')).then(()=>true,()=>false),'Fixture unavailable');
 let s=await snapshot();const global=s.settings.selectedGlobalId,a=s.profiles.find(p=>p.name==='Fixture Game').id;
 async function create(name){const p=(await ok({command:'new',kind:'application',name,executable:path.join(root,name+'.exe')})).profile;const current=await snapshot();await ok({command:'apply',profile:p,expected:null,settings:current.settings,expectedSettings:current.settingsVersion});return p.id;}
 const b=await create('Second'),c=await create('Third');
 const A={id:a,pid:1001,created:10},B={id:b,pid:1002,created:20},C={id:c,pid:1003,created:30},A2={id:a,pid:1004,created:40};
 assert.equal((await scan([A])).activeId,a);assert.equal((await scan([A,B])).activeId,b);assert.equal((await scan([A,B,C])).activeId,c);
 assert.equal((await scan([A,B])).activeId,b);assert.equal((await scan([A])).activeId,a);assert.equal((await scan([])).activeId,global);
 checks.push('A / B / C activation and every fallback through selected Global');
 await scan([A,B]);assert.equal((await scan([A,B,A2])).activeId,b);assert.equal((await scan([B,A2])).activeId,b);
 await pin(a);s=await scan([B,A2,C]);assert.equal(s.activeId,a);assert.equal(s.manualMaskId,a);assert.deepEqual(s.runningOrder,[c,b,a]);
 await pin('');assert.equal((await snapshot()).activeId,c);await pin(a);assert.equal((await scan([B,C])).activeId,c);assert.equal((await snapshot()).manualMaskId,'');
 checks.push('Multiple instances do not promote, manual overrides retain history and expire on final exit');
 await scan([]);await scan([A,B]);await pin(a);
 s=await scan([{...A,exited:50},B,A2]);assert.equal(s.manualMaskId,a);assert.deepEqual(s.runningOrder,[b,a]);
 await pin('');assert.equal((await snapshot()).activeId,b);
 await pin(a);s=await scan([B,{...A2,exited:60},{id:a,pid:A2.pid,created:70}]);assert.equal(s.manualMaskId,'');assert.equal(s.activeId,a);
 await scan([]);
 checks.push('Verified exit times preserve an unsampled instance handoff and distinguish a genuine restart with a recycled PID');
 await scan([A,B]);await pin(a);s=await snapshot();
 const stale=await request({command:'mask',id:b,expected:{...s.profiles.find(p=>p.id===b).version,hash:'0'.repeat(64)},expectedSettings:s.settingsVersion});assert.equal(stale.ok,false);assert.equal((await snapshot()).manualMaskId,a);
 s=await scan([C],true);assert.equal(s.activeId,a);assert.equal(s.manualMaskId,a);await scan([A,B]);
 checks.push('Stale saved versions reject runtime commands and incomplete scans retain the verified policy');
 await pin('');await scan([]);await scan([A,B]);
 s=(await ok({command:'fixture-timeline',samples:[sample([A]),sample([A,B]),sample([A,B,C]),sample([A,B])]})).snapshot;
 assert.equal(s.activeId,b);assert.deepEqual(s.runningOrder,[b,a]);
 checks.push('Worker consumes activation and exit transitions while native UI command processing is blocked');
 s=await snapshot();await ok({command:'settings',settings:{...s.settings,mode:'useGlobal'},expectedSettings:s.settingsVersion});
 await scan([A,B,C]);s=await snapshot();assert.equal(s.activeId,global);assert.equal(s.manualMaskId,'');
 await ok({command:'settings',settings:{...s.settings,mode:'automatic'},expectedSettings:s.settingsVersion});assert.equal((await snapshot()).activeId,c);
 await pin(a);s=await snapshot();await ok({command:'settings',settings:{...s.settings,paused:true},expectedSettings:s.settingsVersion});
 await scan([A,B]);s=await snapshot();assert.equal(s.manualMaskId,'');await ok({command:'settings',settings:{...s.settings,paused:false},expectedSettings:s.settingsVersion});assert.equal((await snapshot()).activeId,b);
 checks.push('Global and Pause clear pins while background activation tracking continues');
 for(const explicit of [{paused:true},{mode:'useGlobal'}]){
  await scan([A]);await pin(a);await scan([B],true);s=await snapshot();
  const saved=await ok({command:'settings',settings:{...s.settings,...explicit},expectedSettings:s.settingsVersion});
  assert.equal(saved.applied,true);s=await snapshot();assert.equal(s.activeId,global);assert.equal(s.verified,true);assert.equal(s.manualMaskId,'');
  assert.equal(s.devices.find(d=>d.identities.includes('HID\\FIXTURE_PEDALS')).current,'Visible');
  assert.deepEqual(s.runningOrder,[a]);
  // A saved edit and Retry must follow the same process-independent admission.
  const {version,running,missing,...profile}=s.profiles.find(p=>p.id===global);profile.name+=' edited';
  const edit=await ok({command:'apply',profile,expected:version,settings:s.settings,expectedSettings:s.settingsVersion});
  assert.match(edit.message,/verified|applied/i);assert.equal((await ok({command:'retry'})).applied,true);
  s=await snapshot();const automatic=await ok({command:'settings',settings:{...s.settings,paused:false,mode:'automatic'},expectedSettings:s.settingsVersion});
  assert.equal(automatic.applied,false);s=await snapshot();assert.equal(s.activeId,global);assert.deepEqual(s.runningOrder,[a]);
  assert.equal((await scan([A,B])).activeId,b);
  // Start with a hidden A again, fail the explicit-mode write, then require
  // the worker Tick path to change and verify visibility while incomplete.
  await scan([A]);await scan([B],true);s=await snapshot();
  await ok({command:'fixture-reconcile-failure',fail:true});
  const deferred=await ok({command:'settings',settings:{...s.settings,...explicit},expectedSettings:s.settingsVersion});assert.equal(deferred.applied,false);
  await ok({command:'fixture-reconcile-failure',fail:false});
  await ok({command:'fixture-timeline',samples:[sample([A]),sample([B],true)]});
  await until(async()=>{const current=await snapshot();return current.verified&&current.activeId===global;},'Explicit mode was not verified after incomplete worker scan');
  s=await snapshot();assert.equal(s.devices.find(d=>d.identities.includes('HID\\FIXTURE_PEDALS')).current,'Visible');
  await ok({command:'settings',settings:{...s.settings,paused:false,mode:'automatic'},expectedSettings:s.settingsVersion});await scan([A,B]);
 }
 checks.push('Saved Pause and Use Global enforce visible policy during incomplete detection; edits, Retry and worker Tick remain consistent');
 checks.push('Returning to Automatic during incomplete detection retains history without inventing exits or promotions');
 await pin(a);await ok({command:'fixture-reconcile-failure',fail:true});const failed=await pin(b);assert.equal(failed.applied,false);s=await snapshot();assert.equal(s.requestedId,b);assert.equal(s.verified,false);
 await ok({command:'fixture-reconcile-failure',fail:false});await ok({command:'retry'});s=await snapshot();assert.equal(s.activeId,b);assert.equal(s.verified,true);
 checks.push('Driver readback failure distinguishes requested mask from verified active policy and remains retryable');
 const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;app=await electron.launch({args:[editor,'--fixture-native',root],env});const page=await app.firstWindow();
 await expect(page.getByText('Profile engine running',{exact:true})).toBeVisible({timeout:20000});
 await page.locator('.profile-item').filter({hasText:'Fixture Game'}).click();await page.getByRole('button',{name:'Profile actions',exact:true}).click();
 await page.getByLabel('Name',{exact:true}).fill('Unsaved draft name');await page.getByRole('button',{name:'Done',exact:true}).click();
 await page.getByLabel('Running application mask',{exact:true}).selectOption(a);await page.getByRole('button',{name:'Use this mask',exact:true}).click();
 await until(async()=>(await snapshot()).manualMaskId===a,'Editor mask command did not reach native service');
 await expect(page.getByRole('heading',{name:'Unsaved draft name',exact:true})).toBeVisible();await expect(page.getByRole('button',{name:'Apply changes',exact:true})).toBeEnabled();
 await page.getByRole('button',{name:'Return to Automatic',exact:true}).click();await until(async()=>(await snapshot()).activeId===b,'Automatic mask did not return');
 await expect(page.getByRole('heading',{name:'Unsaved draft name',exact:true})).toBeVisible();
 await fs.mkdir(output,{recursive:true});await page.screenshot({path:path.join(output,'active-mask-draft.png')});
 checks.push('Actual Electron runtime chooser and Return to Automatic preserve an unsaved profile draft');
 await page.getByRole('button',{name:'Close editor',exact:true}).click();
 await expect(page.getByRole('dialog')).toBeVisible();
 await Promise.all([app.waitForEvent('close',{timeout:10000}),page.getByRole('button',{name:'Discard changes',exact:true}).click({timeout:10000})]);app=null;
 checks.push('Close editor confirms disposal of the unsaved draft and exits');
 await pin(a);s=await snapshot();const {version,running,missing,...profile}=s.profiles.find(p=>p.id===a);profile.enabled=false;
 await ok({command:'apply',profile,expected:version,settings:s.settings,expectedSettings:s.settingsVersion});s=await snapshot();assert.equal(s.manualMaskId,'');assert.equal(s.activeId,b);
 checks.push('Disabling pinned saved profile invalidates its override without promoting edits');
 await ok({command:'fixture-stop'});await fs.writeFile(path.join(output,'result.json'),JSON.stringify({checks,root,realDriverUsed:false},null,2));console.log(`${checks.length} recent-mask fixture groups passed.`);
}finally{
 try{if(app)await stopFixtureTree(app.process());}
 finally{
  if(!exited(engine))await request({command:'fixture-stop'}).catch(()=>{});
  try{await until(()=>exited(engine),'Fixture engine did not exit');}
  finally{await stopFixtureTree(engine);}
 }
}
