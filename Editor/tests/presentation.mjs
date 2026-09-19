import {_electron as electron,expect} from '@playwright/test';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
const root=path.resolve(import.meta.dirname,'..'),output=path.resolve(root,'../artifacts/performance-ui-validation');
await fs.mkdir(output,{recursive:true});
const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
const executable=process.env.HIDHIDE_TEST_ICON_EXE||'';
if(executable)await fs.access(executable);
const app=await electron.launch({args:[root,'--fixture'],env}),checks=[];
try{
 const page=await app.firstWindow(),errors=[];page.on('pageerror',error=>errors.push(error.message));
 const control=options=>app.evaluate((_electron,value)=>globalThis.__fixtureControl(value),options);
 await control({reset:true,presentation:true,executable});
 await expect(page.getByText('Old steering wheel',{exact:true})).toBeVisible({timeout:10000});
 for(const kind of ['wheel','pedals','keypad','camera','headphones','microphone','keyboard','mouse','gamepad','unknown'])await expect(page.locator(`[data-device-kind="${kind}"]`).first()).toBeAttached();
 checks.push('Distinct artwork for wheel, pedals, Stream Deck, camera, headphones, microphone, keyboard, mouse, gamepad and unknown');
 if(executable){
  const icon=page.locator('.application-card .application-icon img');await expect(icon).toBeVisible({timeout:10000});
  const src=await icon.getAttribute('src');assert(src.startsWith('data:image/png;base64,'));
  await fs.writeFile(path.join(output,'application-icon.png'),Buffer.from(src.split(',')[1],'base64'));
  checks.push('Actual installed executable icon extracted asynchronously and rendered in the application card');
 }
 const wheel=page.getByRole('row').filter({has:page.getByText('Simucube 2 Pro',{exact:true})});
 await wheel.getByLabel('Visible',{exact:true}).check();
 await expect(page.getByText('1 pending change',{exact:true})).toBeVisible();
 await page.getByRole('button',{name:'Hide disconnected (1)',exact:true}).click();
 await expect(page.getByText('Old steering wheel',{exact:true})).toHaveCount(0);
 await expect(page.getByText('1 pending change',{exact:true})).toBeVisible();
 await page.getByRole('button',{name:'Apply changes',exact:true}).click();
 await expect(page.getByText('No pending changes',{exact:true})).toBeVisible();
 const state=await control({});assert.equal(state.profiles[1].deviceRules.find(rule=>rule.identity==='remembered').visibility,'hidden');
 checks.push('One-click disconnected filter preserves pending edits and saved remembered-device rules');
 await page.reload();await expect(page.getByRole('button',{name:'Show disconnected (1)',exact:true})).toBeVisible();
 await expect(page.getByText('Old steering wheel',{exact:true})).toHaveCount(0);
 checks.push('Disconnected filter preference survives editor reload');
 await page.locator('.editor-scroll').evaluate(element=>element.scrollTop=0);
 await page.screenshot({path:path.join(output,'icons-and-filter.png')});
 await control({deviceError:'Fixture connection discovery unavailable'});
 await expect(page.getByText('Old steering wheel',{exact:true})).toBeVisible({timeout:10000});
 await expect(page.getByRole('button',{name:'Show disconnected',exact:true})).toBeDisabled();
 checks.push('Unknown connection state cannot silently filter devices away');
 assert.deepEqual(errors,[]);
 await fs.writeFile(path.join(output,'presentation.json'),JSON.stringify({checks,actualExecutableIconTested:!!executable,errors},null,2));
 console.log(`${checks.length} icon/filter presentation groups passed.`);
}finally{await app.close().catch(()=>{});}
