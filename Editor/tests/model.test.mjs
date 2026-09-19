import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {makeDraft,dirty,visibility,setVisibility,devicesFor,deviceRows,filterDeviceRows,validation} from '../src/model.ts';
const fixture=createRequire(import.meta.url)('../electron/fixture.cjs');
test('draft edits never mutate applied snapshot; grouped mixed identities stay explicit',()=>{
 const s=fixture.control({reset:true});const d=makeDraft(s.profiles[1],s);assert.equal(dirty(d),false);
 const group={...s.devices[0],identities:['wheel','other']};assert.equal(visibility(d.profile,group),'mixed');
 d.profile=setVisibility(d.profile,group,'hidden');assert.equal(dirty(d),true);assert.equal(d.profile.deviceRules.length,3);assert.equal(s.profiles[1].deviceRules.length,2);assert.equal(visibility(d.profile,group),'hidden');
});
test('disconnected rules remain visible and unrelated identities survive edits',()=>{
 const s=fixture.control({reset:true});const d=makeDraft(s.profiles[1],s);d.profile.deviceRules.push({identity:'gone',friendlyName:'Old controller',visibility:'hidden'});
 const rows=devicesFor(d.profile,s.devices);assert.equal(rows.at(-1).connected,false);assert.equal(rows.at(-1).current,'Unknown');
 const p=setVisibility(d.profile,s.devices[0],'visible');assert.equal(p.deviceRules.find(r=>r.identity==='gone').visibility,'hidden');
});
test('selected Global cannot be deleted or disabled without another fallback',()=>{
 const s=fixture.control({reset:true});const d=makeDraft(s.profiles[0],s);d.deleting=true;assert.match(validation(d,s),/another enabled Global/);d.deleting=false;d.profile.enabled=false;assert.match(validation(d,s),/another enabled Global/);
});
test('settings edits and new disabled import are unapplied drafts',()=>{
 const s=fixture.control({reset:true});const d=makeDraft(s.profiles[1],s);d.settings.allowedApplications.push('C:\\Feeder.exe');assert.equal(s.settings.allowedApplications.length,1);assert.equal(dirty(d),true);
 const created=makeDraft({...s.profiles[1],enabled:false},s,true);assert.equal(created.expected,null);assert.equal(dirty(created),true);
});
test('disconnected filtering is presentation-only, preserves pending rules and shows unknown connections',()=>{
 const s=fixture.control({reset:true}),draft=makeDraft(s.profiles[1],s);
 draft.profile.deviceRules.push({identity:'gone',friendlyName:'Remembered pedals',visibility:'hidden'});
 const before=JSON.stringify(draft),rows=deviceRows(draft.profile,draft.original,s.devices);
 const visible=filterDeviceRows(rows,true,true);
 assert.equal(visible.length,s.devices.length);assert.equal(rows.length,s.devices.length+1);
 assert.equal(filterDeviceRows(rows,true,false),rows);assert.equal(filterDeviceRows(rows,false,true),rows);
 assert.equal(JSON.stringify(draft),before);assert.equal(dirty(draft),true);
 for(const row of rows)assert.equal(row.value,visibility(draft.profile,row.device));
});
