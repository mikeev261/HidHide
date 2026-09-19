// Shell icon extraction stays off the connection/snapshot path. Only local
// executable icons are exposed; this is not a renderer-readable file endpoint.
const fs=require('node:fs/promises');
const path=require('node:path');
function createAppIconLoader(app,{stat=fs.stat,now=Date.now,maxEntries=128,concurrency=4}={}){
 const cache=new Map(),waiting=[];let active=0;
 const drain=()=>{while(active<concurrency&&waiting.length){active++;const job=waiting.shift();Promise.resolve().then(job.work).then(job.resolve,()=>job.resolve(null)).finally(()=>{active--;drain();});}};
 return function load(executable){
  if(typeof executable!=='string'||executable.length>32768||! /^[a-z]:[\\/]/i.test(executable)||/[\x00-\x1f]/.test(executable)||path.win32.extname(executable).toLowerCase()!=='.exe')return Promise.resolve(null);
  const file=path.win32.normalize(executable),key=file.toLowerCase(),cached=cache.get(key);
  if(cached&&cached.expires>now()){cache.delete(key);cache.set(key,cached);return cached.promise;}
  if(waiting.length>=maxEntries)return Promise.resolve(null);
  const promise=new Promise(resolve=>{waiting.push({resolve,work:async()=>{
   const info=await stat(file);if(!info.isFile())return null;
   const icon=await app.getFileIcon(file,{size:'large'});
   if(icon.isEmpty())return null;
   const url=icon.resize({width:48,height:48,quality:'best'}).toDataURL();
   return url.startsWith('data:image/png;base64,')&&url.length<=128*1024?url:null;
  }});});
  const entry={promise,expires:now()+300000};
  promise.then(result=>{if(!result)entry.expires=now()+5000;});
  cache.set(key,entry);while(cache.size>maxEntries)cache.delete(cache.keys().next().value);
  drain();return promise;
 };
}
module.exports={createAppIconLoader};
