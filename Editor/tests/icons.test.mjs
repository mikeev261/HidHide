import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {deviceKind} from '../src/device-kind.ts';
const {createAppIconLoader}=createRequire(import.meta.url)('../electron/app-icons.cjs');
const icon={isEmpty:()=>false,resize(){return this;},toDataURL:()=> 'data:image/png;base64,fixture'};
test('application icon reads deduplicate, stay bounded, cache failures and reject non-local executables',async()=>{
 let loads=0,active=0,peak=0,stats=0,time=0;
 const app={getFileIcon:async()=>{loads++;active++;peak=Math.max(peak,active);await new Promise(resolve=>setTimeout(resolve,10));active--;return icon;}};
 const load=createAppIconLoader(app,{stat:async()=>{stats++;return {isFile:()=>true};},now:()=>time});
 for(const bad of ['https://example.com/game.exe','\\\\server\\share\\game.exe','C:game.exe','C:\\secret.txt',null])assert.equal(await load(bad),null);
 assert.equal(stats,0);
 const results=await Promise.all(Array.from({length:20},()=>load('C:\\Games\\LMU.exe')));
 assert(results.every(result=>result===icon.toDataURL()));assert.equal(loads,1);
 await load('c:\\games\\lmu.EXE');assert.equal(loads,1);
 await Promise.all(Array.from({length:20},(_,index)=>load(`C:\\Games\\game${index}.exe`)));
 assert(peak<=4);time=300001;await load('C:\\Games\\LMU.exe');assert.equal(loads,22);
 const unavailable=createAppIconLoader({getFileIcon(){throw Error('missing');}},{stat:async()=>{throw Error('absent');}});
 assert.equal(await unavailable('C:\\Missing.exe'),null);
 let available=false;
 const transient=createAppIconLoader(app,{stat:async()=>{if(!available)throw Error('not installed yet');return {isFile:()=>true};},now:()=>time});
 assert.equal(await transient('C:\\Installing.exe'),null);available=true;time+=5001;
 assert.equal(await transient('C:\\Installing.exe'),icon.toDataURL());
});
test('device types distinguish peripherals and preserve an honest unknown fallback',()=>{
 for(const [name,kind] of [['Stream Deck XL','streamdeck'],['Logitech BRIO webcam','camera'],['Arctis headset','headphones'],['Simucube 2 Pro','wheel'],['Simucube ActivePedal','pedals'],['Heusinkveld Ultimate+','pedals'],['Heusinkveld Handbrake','handbrake'],['Microphone','microphone'],['USB keyboard','keyboard'],['Mouse','mouse'],['Xbox controller','gamepad'],['Unidentified USB interface','unknown']])assert.equal(deviceKind({name,detail:''}),kind,name);
 assert.equal(deviceKind({name:'USB device',detail:'',kind:'camera'}),'camera');
});
