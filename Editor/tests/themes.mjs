import {_electron as electron,expect} from '@playwright/test';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
const root=path.resolve(import.meta.dirname,'..'),output=path.resolve(root,'../artifacts/theme-validation');
await fs.mkdir(output,{recursive:true});const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
const app=await electron.launch({args:[root,'--fixture'],env}),checks=[];
try{
 const page=await app.firstWindow(),errors=[];page.on('pageerror',error=>errors.push(error.message));
 await expect(page.getByText('Profile engine running',{exact:true})).toBeVisible();
 const control=options=>app.evaluate((_electron,value)=>globalThis.__fixtureControl(value),options);
 const row=page.getByRole('row').filter({has:page.getByText('Button box',{exact:true})});
 await row.getByLabel('Hidden',{exact:true}).check();
 const initial=await control({});
 for(const theme of ['dark','light']){
  if(theme==='light')await page.getByRole('button',{name:'Switch to light mode',exact:true}).click();
  await expect(page.locator('html')).toHaveAttribute('data-theme',theme);
  assert.equal(await app.evaluate(({nativeTheme})=>nativeTheme.themeSource),theme);
  await expect(page.getByText('1 pending change',{exact:true})).toBeVisible();
  assert.deepEqual((await control({})).profiles,initial.profiles);
  const ratios=await page.evaluate(()=>{
   const css=getComputedStyle(document.documentElement),rgb=name=>{let value=css.getPropertyValue(name).trim().slice(1);if(value.length===3)value=[...value].map(x=>x+x).join('');return value.match(/.{2}/g).map(x=>parseInt(x,16)/255);};
   const luminance=c=>c.map(x=>x<=.04045?x/12.92:((x+.055)/1.055)**2.4).reduce((s,x,i)=>s+x*[.2126,.7152,.0722][i],0);
   const ratio=(a,b)=>{const x=luminance(a),y=luminance(b);return(Math.max(x,y)+.05)/(Math.min(x,y)+.05);};
   const pairs=[['--ink','--background'],['--muted','--background'],['--muted','--surface'],['--selected-ink','--selected'],['--danger','--error-surface'],['--amber','--warning-surface']];
   const values=pairs.map(([a,b])=>({pair:a+'/'+b,ratio:ratio(rgb(a),rgb(b))}));values.push({pair:'primary label',ratio:ratio([1,1,1],rgb('--primary'))});
   const background=rgb('--background');if(background[0]!==background[1]||background[1]!==background[2])throw Error('Surface is not neutral grey');return values;
  });
  for(const value of ratios)assert(value.ratio>=4.5,theme+' '+JSON.stringify(value));
  await page.screenshot({path:path.join(output,theme+'-desktop.png')});
  await page.getByRole('button',{name:'Settings',exact:true}).click();await page.screenshot({path:path.join(output,theme+'-settings.png')});await page.keyboard.press('Escape');
  await page.getByRole('button',{name:'Profile actions',exact:true}).click();await page.screenshot({path:path.join(output,theme+'-actions.png')});await page.keyboard.press('Escape');
  const window=await app.browserWindow(page);
  for(const zoom of [1,1.25,1.5,2]){
   await window.evaluate((window,zoom)=>{window.setSize(600,560);window.webContents.setZoomFactor(zoom);},zoom);await page.waitForTimeout(120);
   assert.equal(await page.evaluate(()=>document.querySelector('.app-shell').scrollWidth>innerWidth),false);
   const toggle=page.getByRole('button',{name:theme==='dark'?'Switch to light mode':'Switch to dark mode',exact:true});await toggle.scrollIntoViewIfNeeded();
   const bounds=await toggle.boundingBox(),size=await page.evaluate(()=>({w:innerWidth,h:innerHeight}));assert(bounds.x>=0&&bounds.x+bounds.width<=size.w+1);
  }
  await fs.writeFile(path.join(output,theme+'-compact.png'),await window.evaluate(async window=>(await window.webContents.capturePage()).toPNG()));
  await window.evaluate(window=>{window.setSize(1320,900);window.webContents.setZoomFactor(1);});
  checks.push({theme,ratios,draftPreserved:true,nativeTheme:true,compactZoom:true});
 }
 await page.getByRole('button',{name:'Discard',exact:true}).click();await page.reload();await expect(page.locator('html')).toHaveAttribute('data-theme','light');
 const prefs=await app.evaluate(({app})=>app.getPath('userData'));assert.equal(JSON.parse(await fs.readFile(path.join(prefs,'editor-appearance.json'),'utf8')).theme,'light');
 await fs.mkdir(path.join(prefs,'editor-appearance.json.tmp'));
 await page.getByRole('button',{name:'Switch to dark mode',exact:true}).click();await expect(page.getByRole('alert')).toContainText('Could not save appearance');await expect(page.locator('html')).toHaveAttribute('data-theme','light');
 await fs.rmdir(path.join(prefs,'editor-appearance.json.tmp'));
 await control({disconnected:true});await expect(page.getByText('Profile engine unavailable',{exact:true})).toBeVisible({timeout:10000});
 await page.screenshot({path:path.join(output,'light-offline.png')});
 await page.getByRole('button',{name:'Switch to dark mode',exact:true}).click();await expect(page.locator('html')).toHaveAttribute('data-theme','dark');await page.reload();await expect(page.locator('html')).toHaveAttribute('data-theme','dark');
 assert.deepEqual(errors,[]);await fs.writeFile(path.join(output,'themes.json'),JSON.stringify({checks,persistence:true,saveFailurePreservesTheme:true,offlineToggle:true,errors},null,2));console.log('Both themes, contrast, dialogs, draft preservation, zoom, persistence and save failure passed.');
}finally{await app.close().catch(()=>{});}
