// Read-only helpers have their own lifetime, outside the serialized policy bridge.
const {spawn}=require('node:child_process');
const path=require('node:path');
function validPath(value){return typeof value==='string'&&value.length<=32767&&/^[a-z]:[\\/]/i.test(value)&&!/[\x00-\x1f]/.test(value)&&!value.slice(2).includes(':')&&path.win32.extname(value).toLowerCase()==='.exe';}
function createApplicationDiscovery(native,{spawnProcess=spawn,timeout=15000}={}){
 let active=null;const cache=new Map();
 function cancel(){active?.cancel();}
 function run(command,selection){
  if(!['list','describe','validate','validate-file'].includes(command))return Promise.reject(Error('Invalid application request.'));
  const request={command};
  if(command!=='list'){
   if(!validPath(selection?.path))return Promise.reject(Error('Choose a local executable file.'));
   request.path=selection.path;
  }
  if(command==='validate'){
   if(!Number.isInteger(selection.pid)||selection.pid<=0||selection.pid>0xffffffff||typeof selection.created!=='string'||!/^\d{1,20}$/.test(selection.created))return Promise.reject(Error('Invalid running application identity.'));
   request.pid=selection.pid;request.created=selection.created;
  }
  cancel();
  const key=command==='describe'?path.win32.normalize(request.path).toLowerCase():null;
  const cached=key&&cache.get(key);
  if(cached&&cached.expires>Date.now())return Promise.resolve(cached.result);
  return new Promise((resolve,reject)=>{
   let child,settled=false,output=Buffer.alloc(0),timer;
   const finish=(error,result)=>{if(settled)return;settled=true;clearTimeout(timer);if(active===operation)active=null;child?.kill();if(error)reject(error);else resolve(result);};
   const operation={cancel:()=>finish(Error('Application selection cancelled.'))};active=operation;
   try{child=spawnProcess(native,['--application-discovery'],{windowsHide:true,stdio:['pipe','pipe','ignore']});}catch(error){finish(error);return;}
   timer=setTimeout(()=>finish(Error('Application discovery timed out. Refresh or browse for the executable.')),timeout);
   child.on('error',error=>finish(error));child.stdin.on('error',error=>finish(error));
   child.stdout.on('data',data=>{if(settled)return;if(output.length+data.length>2*1024*1024){finish(Error('Application response exceeded its limit.'));return;}output=Buffer.concat([output,data]);});
   child.on('close',code=>{
    if(settled)return;
    try{
     if(code!==0)throw Error('Application discovery is unavailable.');
     const result=JSON.parse(output.toString('utf8'));
     if(result.error)throw Error(result.error);
     const applications=command==='list'?result.applications:[result.application];
     if(!Array.isArray(applications)||applications.length>512||applications.some(a=>!a||!validPath(a.path)||typeof a.name!=='string'||a.name.length>256||typeof a.visible!=='boolean'||!Number.isInteger(a.instances)||a.instances<1||!Number.isInteger(a.pid)||typeof a.created!=='string'))throw Error('Invalid application discovery response.');
     if(key){cache.set(key,{result,expires:Date.now()+30000});while(cache.size>128)cache.delete(cache.keys().next().value);}
     finish(null,result);
    }catch(error){finish(error);}
   });
   child.stdin.end(JSON.stringify(request));
  });
 }
 return {run,cancel};
}
module.exports={createApplicationDiscovery};
