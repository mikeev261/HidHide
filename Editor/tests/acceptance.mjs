import {_electron as electron,expect} from '@playwright/test';
import fs from 'node:fs/promises';
import path from 'node:path';
const root=path.resolve(import.meta.dirname,'..'),output=path.resolve(root,'../artifacts/electron-ui-validation');
await fs.mkdir(output,{recursive:true});
const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
const app=await electron.launch({args:[root,'--fixture'],env,timeout:30000});
const errors=[];let passed=[];
try{
 const page=await app.firstWindow();page.on('pageerror',e=>errors.push(e.message));
 const control=options=>app.evaluate((_electron,value)=>globalThis.__fixtureControl(value),options);
 await expect(page.getByRole('heading',{name:'Le Mans Ultimate',exact:true})).toBeVisible();
 await expect(page.getByText('Profile engine running',{exact:true})).toBeVisible();
 await expect(page.getByRole('button',{name:'Apply changes',exact:true})).toBeDisabled();
 passed.push('connected state and initially clean draft');
 const row=page.getByRole('row').filter({has:page.getByText('Button box',{exact:true})});
 await row.getByLabel('Hidden',{exact:true}).check();
 await expect(page.getByText('1 pending change',{exact:true})).toBeVisible();
 await row.getByLabel('Visible',{exact:true}).check();
 await expect(page.getByText('No pending changes',{exact:true})).toBeVisible();
 await expect(page.getByRole('button',{name:'Apply changes',exact:true})).toBeDisabled();
 await expect(row.getByLabel('Changed')).toHaveCount(0);
 passed.push('reversing a live default visibility choice clears pending work');
 await row.getByLabel('Hidden',{exact:true}).check();
 await expect(page.getByText('1 pending change',{exact:true})).toBeVisible();
 await expect(row.getByRole('cell').nth(1)).toContainText('Visible');
 await page.screenshot({path:path.join(output,'desktop-dirty.png')});
 passed.push('changed draft does not claim applied device change');
 await page.getByRole('button',{name:'Default',exact:true}).click();
 await expect(page.getByRole('dialog')).toBeVisible();
 await page.getByRole('button',{name:'Keep editing',exact:true}).click();
 await expect(page.getByRole('heading',{name:'Le Mans Ultimate',exact:true})).toBeVisible();
 await page.getByRole('button',{name:'Apply changes',exact:true}).click();
 await expect(page.getByText('No pending changes',{exact:true})).toBeVisible();
 await expect(row.getByRole('cell').nth(1)).toContainText('Hidden');
 passed.push('dirty navigation cancellation and explicit save');
 await page.getByRole('button',{name:'Default',exact:true}).click();
 await expect(page.getByRole('columnheader',{name:'Profile rule'})).toBeVisible();
 await page.getByRole('button',{name:'Profile actions'}).click();
 await page.getByLabel('Enable this profile').uncheck();
 await page.getByRole('button',{name:'Done',exact:true}).click();
 await expect(page.getByRole('button',{name:'Apply changes',exact:true})).toBeDisabled();
 await page.getByRole('button',{name:'Discard',exact:true}).click();
 passed.push('inactive-profile semantics and invalid Global change blocked');
 await page.getByRole('button',{name:/Le Mans Ultimate/}).click();
 await page.getByRole('button',{name:'Manage allowed apps'}).click();
 await page.getByRole('button',{name:'Add application',exact:true}).click();
 await expect(page.getByRole('dialog')).toContainText('C:\\Games\\New game.exe');
 await page.keyboard.press('Escape');
 await expect(page.getByRole('button',{name:'Apply changes',exact:true})).toBeEnabled();
 await page.getByRole('button',{name:'Discard',exact:true}).click();
 passed.push('allowed apps staged and Escape returns to editor');
 const sizes=[[1320,900],[1000,740],[800,620],[600,560],[1440,960]];
 for(const [width,height] of sizes){
  await app.browserWindow(page).then(w=>w.evaluate((window,{width,height})=>window.setSize(width,height),{width,height}));
  await page.waitForTimeout(120);
  const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>window.innerWidth||document.documentElement.scrollHeight>window.innerHeight);
  if(overflow)throw Error(`Document overflows at ${width}x${height}`);
  await expect(page.getByRole('button',{name:'Close editor',exact:true})).toBeVisible();
  await page.screenshot({path:path.join(output,`layout-${width}.png`)});
 }
 for(let i=0;i<6;i++){const w=await app.browserWindow(page);await w.evaluate(window=>window.maximize());await w.evaluate(window=>window.unmaximize());}
 async function reachable(locator){
  await locator.scrollIntoViewIfNeeded();
  const bounds=await locator.boundingBox(),viewport=await page.evaluate(()=>({w:innerWidth,h:innerHeight}));
  if(!bounds||bounds.x<0||bounds.y<0||bounds.x+bounds.width>viewport.w+1||bounds.y+bounds.height>viewport.h+1)throw Error('Control clipped: '+await locator.textContent()+JSON.stringify({bounds,viewport}));
 }
 for(const [width,height] of [[1440,960],[600,560]])for(const zoom of [1.25,1.5,2]){
  await app.browserWindow(page).then(w=>w.evaluate((window,{width,height,zoom})=>{window.setSize(width,height);window.webContents.setZoomFactor(zoom);},{width,height,zoom}));
  await page.waitForTimeout(100);
  const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth||document.querySelector('.app-shell').scrollWidth>innerWidth);
  const clippedModes=await page.locator('.engine-controls .segments span').evaluateAll(spans=>spans.some(span=>span.scrollWidth>span.clientWidth));
  if(clippedModes)throw Error(`Mode labels clipped at ${width} @ ${zoom}`);
  if(overflow)throw Error(`Horizontal overflow at ${width}x${height} @ ${zoom}`);
  for(const name of ['Pause hiding','Profile actions','Manage allowed apps','Apply changes','Close editor'])await reachable(page.getByRole('button',{name,exact:true}));
  const compact=page.getByRole('button',{name:'Profiles',exact:true});
  if(await compact.isVisible()){
   await compact.click();await reachable(page.getByLabel('Find a profile'));await reachable(page.getByRole('button',{name:'Settings',exact:true}));
   if(width===600&&zoom===2){
    await page.locator('.sidebar').scrollIntoViewIfNeeded();
    const capture=await app.browserWindow(page).then(w=>w.evaluate(async window=>(await window.webContents.capturePage()).toPNG().toString('base64')));
    await fs.writeFile(path.join(output,'compact-navigation.png'),Buffer.from(capture,'base64'));
   }
   await compact.click();
   if(width===600&&zoom===2){
    await page.getByRole('button',{name:'Profile actions'}).click();
    for(const name of ['Duplicate as disabled draft','Export saved profile','Mark for deletion','Done'])await reachable(page.getByRole('dialog').getByRole('button',{name,exact:true}));
    await page.getByRole('dialog').evaluate(el=>el.scrollTop=0);
    const capture=await app.browserWindow(page).then(w=>w.evaluate(async window=>(await window.webContents.capturePage()).toPNG().toString('base64')));
    await fs.writeFile(path.join(output,'compact-modal.png'),Buffer.from(capture,'base64'));await page.keyboard.press('Escape');
   }
  }
  await page.locator('.app-shell').evaluate(el=>el.scrollTop=0);
  const capture=await app.browserWindow(page).then(w=>w.evaluate(async window=>(await window.webContents.capturePage()).toPNG().toString('base64')));
  await fs.writeFile(path.join(output,`zoom-${width}-${zoom}.png`),Buffer.from(capture,'base64'));
 }
 await app.browserWindow(page).then(w=>w.evaluate(window=>{window.webContents.setZoomFactor(1);window.setSize(1440,960);}));
 passed.push('five window sizes, repeated maximize/restore and 125/150/200 percent zoom');
 await control({unknown:true});await expect(page.getByText('Profile engine needs attention',{exact:true})).toBeVisible();
 await expect(page.getByRole('row').filter({has:page.getByText('Button box',{exact:true})}).getByRole('cell').nth(1)).toContainText('Unknown');
 await page.screenshot({path:path.join(output,'unknown-state.png')});
 await control({unknown:false,disconnected:true});await expect(page.getByText('Profile engine unavailable',{exact:true})).toBeVisible();
 await expect(page.getByText('Engine connection unavailable. Applied state is unknown.')).toBeVisible();
 await control({disconnected:false});await page.getByRole('button',{name:'Reconnect',exact:true}).click();
 await expect(page.getByText('Profile engine running',{exact:true})).toBeVisible();
 await expect(page.getByText('Fixture engine disconnected.',{exact:true})).toHaveCount(0);
 passed.push('unknown and disconnected state never masquerade as observed Hidden/Visible');
 // Navigation after Apply must use the newly saved settings version.
 await page.getByRole('button',{name:'Pause hiding',exact:true}).click();
 await page.getByRole('button',{name:'Default',exact:true}).click();
 await page.getByRole('dialog').getByRole('button',{name:'Apply changes',exact:true}).click();
 await expect(page.getByRole('heading',{name:'Default',exact:true})).toBeVisible();
 await page.getByRole('button',{name:'Resume hiding',exact:true}).click();
 await page.getByRole('button',{name:'Apply changes',exact:true}).click();
 await expect(page.getByText('No pending changes',{exact:true})).toBeVisible();
 await expect(page.getByRole('heading',{name:'Default',exact:true})).toBeVisible();
 passed.push('Apply during navigation refreshes target versions and preserves inactive selection');
 await page.getByRole('button',{name:'Profile actions'}).click();
 await control({fail:'Fixture export failure'});await page.getByRole('button',{name:'Export saved profile'}).click();
 await expect(page.getByRole('dialog').getByRole('alert')).toContainText('Fixture export failure');await page.keyboard.press('Escape');
 await page.getByRole('button',{name:'Settings',exact:true}).click();await control({fail:'Fixture backup failure'});
 await page.getByRole('button',{name:'Back up saved profiles'}).click();await expect(page.getByRole('dialog').getByRole('alert')).toContainText('Fixture backup failure');await page.keyboard.press('Escape');
 passed.push('Modal file-operation failures are visible inside the initiating dialog');
 await page.getByRole('button',{name:'Device details',exact:true}).first().click();await control({unknown:true});
 await expect(page.getByRole('dialog')).toContainText('Applied now: Unknown');await control({disconnected:true});
 await expect(page.getByRole('dialog')).toContainText('Connection unknown');await page.keyboard.press('Escape');
 await control({unknown:false,disconnected:false});await page.getByRole('button',{name:'Reconnect',exact:true}).click();
 passed.push('Open device details refresh observation and connection status');
 await control({long:true,many:true});await page.waitForTimeout(2100);await page.screenshot({path:path.join(output,'long-many.png')});
 await expect(page.getByRole('button',{name:'Close editor',exact:true})).toBeVisible();
 await page.getByLabel('Find a profile').fill('not a profile');await expect(page.getByText('No profiles match', {exact:false})).toBeVisible();
 await page.getByLabel('Find a profile').fill('');
 passed.push('long Unicode names, many rows and no-match search');
 await control({reset:true});await page.waitForTimeout(2000);
 await page.getByRole('button',{name:'New profile',exact:true}).click();await page.getByLabel('Profile name').fill('New fixture game');
 await page.getByRole('button',{name:'Choose application',exact:true}).click();await expect(page.getByRole('heading',{name:'New fixture game',exact:true})).toBeVisible();
 await page.getByRole('button',{name:'Close editor',exact:true}).click();await expect(page.getByRole('dialog')).toBeVisible();
 await page.getByRole('button',{name:'Keep editing',exact:true}).click();
 await control({fail:'Injected save failure: draft must survive.'});await page.getByRole('button',{name:'Apply changes',exact:true}).click();
 await expect(page.getByText('Injected save failure: draft must survive.',{exact:true})).toBeVisible();
 await expect(page.getByRole('heading',{name:'New fixture game',exact:true})).toBeVisible();
 passed.push('new profile remains detached; failed save and cancelled close preserve draft');
 const process=app.process();await page.getByRole('button',{name:'Close editor',exact:true}).click();await page.getByRole('button',{name:'Discard changes',exact:true}).click().catch(error=>{if(!error.message.includes('closed'))throw error;});
 await new Promise((resolve,reject)=>{if(process.exitCode!==null)return resolve();const timer=setTimeout(()=>reject(Error('Editor did not exit')),10000);process.once('exit',()=>{clearTimeout(timer);resolve();});});
 passed.push('Close editor terminates Electron main process normally');
 if(errors.length)throw Error(errors.join('\n'));
 await fs.writeFile(path.join(output,'acceptance.json'),JSON.stringify({passed,errors,fixture:true},null,2));console.log(`${passed.length} Electron UI acceptance groups passed.`);
}finally{await app.close().catch(()=>{});}
