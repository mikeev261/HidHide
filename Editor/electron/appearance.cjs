const fs=require('node:fs/promises');
const path=require('node:path');
const valid=value=>value==='light'||value==='dark';
function createAppearanceStore(directory){
 const file=path.join(directory,'editor-appearance.json');let current='dark',queue=Promise.resolve();
 return {
  async load(){try{const text=await fs.readFile(file,'utf8');if(text.length<1024){const value=JSON.parse(text).theme;if(valid(value))current=value;}}catch{}return current;},
  get(){return current;},
  set(value){if(!valid(value))return Promise.reject(Error('Unknown appearance.'));const save=queue.then(async()=>{
   await fs.mkdir(directory,{recursive:true});await fs.writeFile(file+'.tmp',JSON.stringify({theme:value})+'\n','utf8');await fs.rename(file+'.tmp',file);current=value;return value;
  });queue=save.catch(()=>{});return save;}
 };
}
module.exports={createAppearanceStore};
