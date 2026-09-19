import {_electron as electron,expect} from '@playwright/test';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
const root=path.resolve(import.meta.dirname,'..');
const executable=path.join(root,'out/HidHideProfiles-win32-x64/HidHideProfiles.exe');
const output=path.resolve(root,'../artifacts/electron-ui-validation');
// This layout intentionally has no native sibling: testing the packaged renderer
// must never auto-start the installed coordinator or touch the real driver.
assert.equal(await fs.access(path.join(root,'out/HidHideClient.exe')).then(()=>true,()=>false),false);
const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;
const app=await electron.launch({executablePath:executable,args:[],env});
try{
 const page=await app.firstWindow(),errors=[];page.on('pageerror',error=>errors.push(error.message));
 await expect(page.getByText('Profile engine unavailable',{exact:true})).toBeVisible();
 await expect(page.getByRole('heading',{name:'Connecting to HidHide'})).toBeVisible();
 await expect(page.getByText('Current policy is unknown',{exact:true})).toBeVisible();
 const security=await app.evaluate(({app,BrowserWindow})=>({packaged:app.isPackaged,preferences:BrowserWindow.getAllWindows()[0].webContents.getLastWebPreferences()}));
 assert.equal(security.packaged,true);assert.equal(security.preferences.nodeIntegration,false);assert.equal(security.preferences.contextIsolation,true);assert.equal(security.preferences.sandbox,true);assert.equal(security.preferences.webSecurity,true);
 assert.equal(await page.evaluate(()=>typeof window.require),'undefined');
 await page.screenshot({path:path.join(output,'packaged-offline.png')});
 const child=app.process();const ended=new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(Error('Packaged editor failed to exit')),10000);child.once('exit',()=>{clearTimeout(timeout);resolve();});});
 await page.getByRole('button',{name:'Close editor',exact:true}).click().catch(error=>{if(!error.message.includes('closed'))throw error;});await ended;
 assert.deepEqual(errors,[]);
 await fs.writeFile(path.join(output,'packaged-smoke.json'),JSON.stringify({packaged:true,offlineAssetsLoaded:true,missingEngineShownAsUnknown:true,normalExit:true,rendererErrors:errors,security},null,2));
 console.log('Packaged offline renderer, security preferences and normal exit passed.');
}finally{await app.close().catch(()=>{});}
