import type {Profile, CatalogProfile, Snapshot, Draft, Device, Settings, Rule} from './types.ts';
export const clone = <T,>(value:T):T => structuredClone(value);
export function profileData(p:Profile|CatalogProfile):Profile {
 return {schemaVersion:1,id:p.id,revision:p.revision,name:p.name,kind:p.kind,enabled:p.enabled,priority:p.priority,executablePath:p.executablePath,defaultVisibility:'visible',deviceRules:clone(p.deviceRules)};
}
export function makeDraft(p:CatalogProfile|Profile,s:Snapshot,isNew=false):Draft {
 const profile=profileData(p);
 return {profile,settings:clone(s.settings),original:isNew?null:clone(profile),originalSettings:clone(s.settings),expected:isNew?null:('version' in p?p.version:null),expectedSettings:s.settingsVersion,deleting:false};
}
export const same=(a:unknown,b:unknown)=>JSON.stringify(a)===JSON.stringify(b);
export const dirty=(d:Draft|null)=>!!d&&(d.deleting||!same(d.profile,d.original)||!same(d.settings,d.originalSettings));
export function visibility(p:Profile,device:Device,index?:Map<string,Rule['visibility']>):'hidden'|'visible'|'mixed' {
 const values=device.identities.map(id=>(index?index.get(id.toLowerCase()):p.deviceRules.find(r=>r.identity.toLowerCase()===id.toLowerCase())?.visibility)??'visible');
 return values.every(v=>v==='hidden')?'hidden':values.every(v=>v==='visible')?'visible':'mixed';
}
export function deviceRows(profile:Profile,original:Profile|null,devices:Device[]){
 const index=new Map(profile.deviceRules.map(rule=>[rule.identity.toLowerCase(),rule.visibility]));
 const before=original?new Map(original.deviceRules.map(rule=>[rule.identity.toLowerCase(),rule.visibility])):null;
 return devicesFor(profile,devices).map(device=>{
  const value=visibility(profile,device,index);
  return {device,value,changed:!original||visibility(original,device,before!)!==value};
 });
}
export function filterDeviceRows<T extends {device:Device}>(rows:T[],hideDisconnected:boolean,connectionKnown:boolean):T[]{
 return hideDisconnected&&connectionKnown?rows.filter(row=>row.device.connected):rows;
}
export function setVisibility(p:Profile,device:Device,value:'hidden'|'visible'):Profile {
 const ids=new Set(device.identities.map(x=>x.toLowerCase()));
 return {...p,deviceRules:[...p.deviceRules.filter(r=>!ids.has(r.identity.toLowerCase())),...device.identities.map(identity=>({identity,friendlyName:device.name,visibility:value}))]};
}
export function devicesFor(profile:Profile,devices:Device[]):Device[] {
 const known=new Set(devices.flatMap(d=>d.identities.map(id=>id.toLowerCase())));
 const remembered=profile.deviceRules.filter(r=>!known.has(r.identity.toLowerCase())).map(r=>({id:r.identity,name:r.friendlyName||r.identity,detail:'Remembered device',connected:false,identities:[r.identity],current:'Unknown' as const}));
 return [...devices,...remembered];
}
export function validation(d:Draft,s:Snapshot):string {
 if(!d.profile.name.trim())return 'Enter a profile name.';
 if(d.profile.name.length>256)return 'Use a profile name of 256 characters or fewer.';
 if(!Number.isInteger(d.profile.priority)||Math.abs(d.profile.priority)>100000)return 'Priority must be a whole number from −100000 to 100000.';
 if(d.profile.kind==='application'&&!/^[a-z]:\\.+\.exe$/i.test(d.profile.executablePath))return 'Choose a local application executable (.exe).';
 const selected=s.profiles.find(p=>p.id===d.settings.selectedGlobalId);
 const selectedDraft=d.profile.id===d.settings.selectedGlobalId?d.profile:selected;
 if(!selectedDraft||selectedDraft.kind!=='global'||!selectedDraft.enabled||d.deleting&&d.profile.id===d.settings.selectedGlobalId)return 'Choose another enabled Global fallback before disabling or deleting this profile.';
 if(s.repositoryIssues.length)return 'Resolve the profile file problems before saving.';
 return '';
}
export function pendingCount(d:Draft):number {
 if(!d.original||d.deleting)return 1;
 let count=0; const a=d.original;
 for(const key of ['name','enabled','priority','executablePath'] as const)if(d.profile[key]!==a[key])count++;
 const rules=new Map(a.deviceRules.map(r=>[r.identity.toLowerCase(),r.visibility]));
 const updated=new Map(d.profile.deviceRules.map(r=>[r.identity.toLowerCase(),r.visibility]));
 for(const id of new Set([...rules.keys(),...updated.keys()]))if((rules.get(id)??'visible')!==(updated.get(id)??'visible'))count++;
 for(const key of Object.keys(d.settings) as (keyof Settings)[])if(!same(d.settings[key],d.originalSettings[key]))count++;
 return Math.max(1,count);
}
