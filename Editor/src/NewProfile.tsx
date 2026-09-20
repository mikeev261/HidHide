import React,{useEffect,useRef,useState} from 'react';
import type {Application} from './types';
import {ApplicationIcon} from './icons';

// Unmount/type/selection changes invalidate every outstanding asynchronous reply.
export function NewProfile({onCreate,onClose,busy}:{onCreate:(kind:'global'|'application',name:string,path:string)=>Promise<boolean>;onClose:()=>void;busy:boolean}){
 const api=window.hidHide;
 const [kind,setKind]=useState<'global'|'application'>('application'),[name,setName]=useState(''),[selected,setSelected]=useState<Application|null>(null);
 const [running,setRunning]=useState<Application[]|null>(null),[search,setSearch]=useState(''),[loading,setLoading]=useState(false),[error,setError]=useState(''),[listNote,setListNote]=useState('');
 const [submitting,setSubmitting]=useState(false);
 const generation=useRef(0),manualName=useRef(false),alive=useRef(true),creating=useRef(false);
 useEffect(()=>()=>{alive.current=false;++generation.current;api.cancelApplicationDiscovery();},[]);
 function invalidate(){++generation.current;api.cancelApplicationDiscovery();setLoading(false);setError('');}
 function changeKind(value:'global'|'application'){invalidate();setKind(value);setSelected(null);setRunning(null);setName('');manualName.current=false;}
 async function browse(){
  invalidate();const ticket=generation.current;setLoading(true);
  try{
   const path=await api.pick('executable');if(ticket!==generation.current||!alive.current)return;
   if(!path)return;
   setSelected(null);
   const result=await api.describeApplication(path);
   if(ticket!==generation.current||!alive.current)return;
   setSelected(result);setRunning(null);if(!manualName.current)setName(result.name);
  }catch(e){if(ticket===generation.current&&alive.current)setError(String(e instanceof Error?e.message:e));}
  finally{if(ticket===generation.current&&alive.current)setLoading(false);}
 }
 async function refresh(){
  invalidate();const ticket=generation.current;setRunning([]);setSelected(null);setLoading(true);setListNote('');
  try{const list=await api.listApplications();if(ticket===generation.current&&alive.current){setRunning(list.applications);setListNote([list.unavailable?`${list.unavailable} processes unavailable because their identity could not be read.`:'',list.truncated?'The list reached its limit. Browse for an EXE if it is missing.':'',list.metadataPartial?'Some names use filenames because metadata lookup reached its time limit.':''].filter(Boolean).join(' '));}}
  catch(e){if(ticket===generation.current&&alive.current)setError(String(e instanceof Error?e.message:e));}
  finally{if(ticket===generation.current&&alive.current)setLoading(false);}
 }
 async function select(application:Application){
  invalidate();const ticket=generation.current;setSelected(null);setLoading(true);
  try{
   const result=await api.validateApplication(application);
   if(ticket!==generation.current||!alive.current)return;
   setSelected({...application,name:result.name});if(!manualName.current)setName(result.name);
  }catch(e){if(ticket===generation.current&&alive.current)setError(String(e instanceof Error?e.message:e));}
  finally{if(ticket===generation.current&&alive.current)setLoading(false);}
 }
 async function create(){
  if(creating.current||loading||busy||!name.trim()||(kind==='application'&&!selected))return;
  creating.current=true;setSubmitting(true);const ticket=generation.current;setLoading(true);setError('');
  try{
   // Validate again at the draft boundary, including file existence and process lifetime.
   if(kind==='application'&&selected){await api.validateApplication(selected);}
   if(ticket!==generation.current||!alive.current)return;
   if(await onCreate(kind,name.trim(),selected?.path||''))onClose();
  }catch(e){if(ticket===generation.current&&alive.current)setError(String(e instanceof Error?e.message:e));}
  finally{creating.current=false;if(alive.current){setSubmitting(false);if(ticket===generation.current)setLoading(false);}}
 }
 const matches=running?.filter(a=>(a.name+' '+a.path).toLocaleLowerCase().includes(search.toLocaleLowerCase()));
 return <>
  <label className="field">Profile type<select value={kind} disabled={busy||submitting} onChange={e=>changeKind(e.target.value as typeof kind)}><option value="application">Application</option><option value="global">Global</option></select></label>
  {kind==='application'&&<>
   <div className="application-actions"><button disabled={busy||loading} onClick={()=>void browse()}>Browse for EXE</button><button disabled={busy||loading} onClick={()=>void refresh()}>Choose running application</button></div>
   {running!==null&&<section aria-label="Running applications">
    <div className="application-actions"><label className="field">Find running application<input type="search" value={search} onChange={e=>setSearch(e.target.value)} placeholder="Name, filename or full path"/></label><button disabled={busy||loading} onClick={()=>void refresh()}>Refresh</button></div>
    <p className="muted">Window apps appear first; background apps are also available. Only apps in your current Windows session are listed.</p>
    {listNote&&<p role="status" className="muted">{listNote}</p>}
    <div className="running-applications">{matches?.map(a=><button key={a.path.toLowerCase()} disabled={busy||loading} aria-pressed={selected?.path===a.path} onClick={()=>void select(a)}><ApplicationIcon path={a.path}/><span><strong>{a.name}</strong><small>{a.path}</small><small>{a.visible?'Window app':'Background app'} · {a.instances} {a.instances===1?'instance':'instances'}</small></span></button>)}</div>
    {!loading&&!matches?.length&&<p>No running applications match. Refresh or browse for an EXE.</p>}
   </section>}
   {selected&&<div className="selected-application"><ApplicationIcon path={selected.path}/><span><strong>Selected application</strong><small>{selected.path}</small></span></div>}
   <p className="muted">Choose the game’s actual executable, which may differ from its launcher. A running app may already have opened its controllers. After saving, close it and use Launch with profile to apply hiding before it starts.</p>
  </>}
  {(kind==='global'||selected)&&<label className="field">Profile name<input autoFocus maxLength={256} value={name} disabled={busy||submitting} onChange={e=>{manualName.current=true;setName(e.target.value);}}/></label>}
  <p className="muted">{kind==='global'?'A Global profile can be selected as your fallback. ':''}The new profile is saved only when you apply.</p>
  {loading&&<p role="status">Reading application…</p>}{error&&<p role="alert" className="error-text">{error}</p>}
  <div className="dialog-actions"><button disabled={busy||submitting} onClick={onClose}>Cancel</button><button className="primary" disabled={busy||loading||!name.trim()||(kind==='application'&&!selected)} onClick={()=>void create()}>Create draft</button></div>
 </>;
}
