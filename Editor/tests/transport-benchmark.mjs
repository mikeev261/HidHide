import {spawn,execFileSync} from 'node:child_process';
import {performance} from 'node:perf_hooks';
import {randomUUID} from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

const native=path.resolve(import.meta.dirname,'../../bin/Release/x64/HidHideClient.exe');
const root=path.join(os.tmpdir(),'HidHide-Profiles-Restart-Test-'+randomUUID());
const count=Number(process.argv[2]||20);
const sessionMode=process.argv.includes('--session');
const idleMs=Number(process.argv.find(arg=>arg.startsWith('--idle-ms='))?.slice(10)||0);
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function exchange(value){
  return new Promise((resolve,reject)=>{
    const started=performance.now();
    const child=spawn(native,['--editor-fixture-request',root],{windowsHide:true,stdio:['pipe','pipe','pipe']});
    const chunks=[];let error='';
    const timer=setTimeout(()=>{child.kill();reject(Error('Bridge timed out'));},15000);
    child.stdout.on('data',chunk=>chunks.push(chunk));
    child.stderr.on('data',chunk=>error+=chunk.toString());
    child.on('error',reject);
    child.on('close',()=>{
      clearTimeout(timer);
      try{const response=JSON.parse(Buffer.concat(chunks).toString('utf8'));if(!response.ok)throw Error(response.error);resolve({ms:performance.now()-started,bytes:Buffer.concat(chunks).length});}
      catch(cause){reject(Error(cause.message+' '+error));}
    });
    child.stdin.end(JSON.stringify(value));
  });
}
function stats(values){const ordered=[...values].sort((a,b)=>a-b);return {min:ordered[0],median:ordered[Math.floor(ordered.length/2)],p90:ordered[Math.ceil(ordered.length*.9)-1],max:ordered.at(-1),mean:values.reduce((a,b)=>a+b,0)/values.length};}
function metrics(pid){const script=`$p=Get-Process -Id ${pid} -ErrorAction Stop; [pscustomobject]@{CpuMs=$p.TotalProcessorTime.TotalMilliseconds;WorkingSet=$p.WorkingSet64;PrivateBytes=$p.PrivateMemorySize64} | ConvertTo-Json -Compress`;return JSON.parse(execFileSync('powershell.exe',['-NoProfile','-Command',script],{encoding:'utf8'}));}
function createSession(){
  const child=spawn(native,['--editor-fixture-session',root],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  let buffer='',pending=null;
  child.stdout.setEncoding('utf8');
  child.stdout.on('data',chunk=>{
    buffer+=chunk;const end=buffer.indexOf('\n');if(end<0)return;
    const line=buffer.slice(0,end);buffer=buffer.slice(end+1);
    const waiting=pending;pending=null;
    try{const response=JSON.parse(line);if(!response.ok)throw Error(response.error);waiting.resolve({ms:performance.now()-waiting.started,bytes:Buffer.byteLength(line)});}catch(error){waiting.reject(error);}
  });
  child.on('error',error=>{if(pending)pending.reject(error);});
  child.on('close',code=>{if(pending)pending.reject(Error(`Session closed (${code})`));});
  return {child,exchange(value){return new Promise((resolve,reject)=>{if(pending)throw Error('Session overlap');pending={resolve,reject,started:performance.now()};child.stdin.write(JSON.stringify(value)+'\n');});}};
}
const started=performance.now();
const host=spawn(native,['--profiles-editor-host',root],{windowsHide:true,stdio:['ignore','pipe','pipe']});
let hostError='';host.stderr.on('data',chunk=>hostError+=chunk.toString());
let session;
try{
  for(let i=0;i<200;i++){if(await fs.access(path.join(root,'editor-host-ready.json')).then(()=>true,()=>false))break;if(host.exitCode!==null)throw Error('Fixture host exited');await sleep(25);}
  if(!await fs.access(path.join(root,'editor-host-ready.json')).then(()=>true,()=>false))throw Error('Fixture host did not become ready');
  const readyMs=performance.now()-started;
  if(sessionMode)session=createSession();
  const samples=[];for(let i=0;i<count;i++){try{samples.push(await (sessionMode?session.exchange({command:'snapshot'}):exchange({command:'snapshot'})));}catch(error){throw Error(`Request ${i+1}: ${error.message}; host exit ${host.exitCode}; stderr ${hostError}`);}}
  let idle;
  if(idleMs){const before=metrics(host.pid);await sleep(idleMs);const after=metrics(host.pid);idle={durationMs:idleMs,cpuMs:after.CpuMs-before.CpuMs,workingSetBytes:after.WorkingSet,privateBytes:after.PrivateBytes};}
  await exchange({command:'fixture-stop'});
  console.log(JSON.stringify({count,sessionMode,readyMs,snapshotMs:stats(samples.map(x=>x.ms)),responseBytes:stats(samples.map(x=>x.bytes)),idle,root,hostPid:host.pid},null,2));
}finally{
  if(session){session.child.stdin.end();session.child.kill();}
  if(host.exitCode===null){await exchange({command:'fixture-stop'}).catch(()=>{});for(let i=0;i<40&&host.exitCode===null;i++)await sleep(50);}
}
