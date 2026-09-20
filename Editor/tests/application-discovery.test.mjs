import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {createRequire} from 'node:module';
const {createApplicationDiscovery}=createRequire(import.meta.url)('../electron/application-discovery.cjs');
const app={path:'C:\\Games\\race.exe',name:'Race',pid:10,created:'123',visible:true,instances:1};
function harness(timeout=1000){
 const children=[];
 const discovery=createApplicationDiscovery('native.exe',{timeout,spawnProcess:(exe,args,options)=>{
  assert.equal(exe,'native.exe');assert.deepEqual(args,['--application-discovery']);assert.equal(options.windowsHide,true);
  const child=new EventEmitter();child.stdout=new EventEmitter();child.stdin=new EventEmitter();child.stdin.end=value=>{child.request=JSON.parse(value);};child.kill=()=>{child.killed=true;};children.push(child);return child;
 }});
 const answer=(child,result)=>{child.stdout.emit('data',Buffer.from(JSON.stringify(result)));child.emit('close',0);};
 return {discovery,children,answer};
}
test('narrow requests reject paths and process identities before spawning',async()=>{
 const {discovery,children}=harness();
 for(const path of ['\\\\server\\share\\a.exe','C:\\Games\\a.exe:stream','C:\\bad.txt','relative.exe'])await assert.rejects(discovery.run('describe',{path}));
 await assert.rejects(discovery.run('validate',{...app,created:'not a lifetime'}));
 await assert.rejects(discovery.run('launch',app));assert.equal(children.length,0);
});
test('new selections cancel old helper and ignore late output',async()=>{
 const {discovery,children,answer}=harness();
 const old=discovery.run('list');const rejected=assert.rejects(old,/cancelled/);
 const next=discovery.run('describe',app);assert.equal(children[0].killed,true);
 answer(children[0],{applications:[app]});answer(children[1],{application:app});
 assert.deepEqual(await next,{application:app});await rejected;
});
test('metadata cache is bounded to describe; validation always invokes native',async()=>{
 const {discovery,children,answer}=harness();
 let response=discovery.run('describe',app);answer(children[0],{application:app});await response;
 await discovery.run('describe',app);assert.equal(children.length,1);
 response=discovery.run('validate',app);answer(children[1],{application:app});await response;
 response=discovery.run('validate-file',{path:app.path});answer(children[2],{application:app});await response;
 assert.equal(children.length,3);
});
test('output limits, malformed responses, failures and timeout release helper',async()=>{
 const {discovery,children,answer}=harness(20);
 let result=discovery.run('list');let rejected=assert.rejects(result,/exceeded/);children[0].stdout.emit('data',Buffer.alloc(2*1024*1024+1));await rejected;assert.equal(children[0].killed,true);
 result=discovery.run('list');rejected=assert.rejects(result,/Invalid/);answer(children[1],{applications:[{...app,path:'relative.exe'}]});await rejected;
 result=discovery.run('list');rejected=assert.rejects(result,/exited/);answer(children[2],{error:'Application exited.'});await rejected;
 await assert.rejects(discovery.run('list'),/timed out/);assert.equal(children[3].killed,true);
});
