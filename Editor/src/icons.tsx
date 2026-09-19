import {memo,useEffect,useRef,useState} from 'react';
import {AppWindow,Camera,Headphones,Mic,Volume2,Keyboard,Mouse,Joystick,Gamepad2,Grid2X2,Usb} from 'lucide-react';
import {deviceKind} from './device-kind';
import type {Device} from './types';

export const ApplicationIcon=memo(function ApplicationIcon({path,size=32}:{path:string;size?:number}){
 const ref=useRef<HTMLSpanElement>(null),[loaded,setLoaded]=useState<{path:string;url:string}|null>(null);
 useEffect(()=>{
  let cancelled=false,visible=false,loading=false,failures=0,timer:ReturnType<typeof setTimeout>;
  const load=async()=>{
   clearTimeout(timer);if(cancelled||loading||!visible||document.hidden||document.documentElement.dataset.editorVisible==='false')return;loading=true;
   const url=await window.hidHide.getAppIcon(path).catch(()=>null);loading=false;
   if(cancelled)return;setLoaded(url?{path,url}:null);failures=url?0:failures+1;
   // Missing executables can appear later. Refresh visible icons with bounded
   // backoff; successful icons share the main-process five-minute cache.
   if(visible&&!document.hidden&&document.documentElement.dataset.editorVisible!=='false')timer=setTimeout(load,url?300000:Math.min(60000,5000*2**Math.min(failures-1,4)));
  };
  const observer=new IntersectionObserver(entries=>{visible=entries.some(entry=>entry.isIntersecting);if(visible)void load();else clearTimeout(timer);});
  const visibility=()=>{if(document.hidden||document.documentElement.dataset.editorVisible==='false')clearTimeout(timer);else if(visible)void load();};
  if(ref.current)observer.observe(ref.current);document.addEventListener('visibilitychange',visibility);window.addEventListener('hidhide-visibility',visibility);
  return()=>{cancelled=true;clearTimeout(timer);observer.disconnect();document.removeEventListener('visibilitychange',visibility);window.removeEventListener('hidhide-visibility',visibility);};
 },[path]);
 return <span className="application-icon" ref={ref} style={{width:size,height:size}} aria-hidden="true">{loaded?.path===path?<img src={loaded.url} width={size} height={size} alt=""/>:<AppWindow size={size}/>}</span>;
});

export const DeviceIcon=memo(function DeviceIcon({device,size=30}:{device:Device;size?:number}){
 const kind=deviceKind(device),common={width:size,height:size,viewBox:'0 0 24 24',fill:'none',stroke:'currentColor',strokeWidth:1.6,strokeLinecap:'round' as const,strokeLinejoin:'round' as const,'aria-hidden':true as const};
 const glyph=kind==='wheel'?<svg {...common}><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="2.5"/><path d="m3 10 6.7 1m11.3-1-6.7 1M12 14.5V21M6 5.3h12"/></svg>
  :kind==='pedals'?<svg {...common}><path d="M3 21h18M6 21l2-9m7 9 2-11"/><rect x="5" y="3" width="6" height="10" rx="1.5" transform="rotate(12 8 8)"/><rect x="14" y="2" width="6" height="9" rx="1.5" transform="rotate(12 17 6)"/><path d="m7 6 2 .4m-2.5 3 2 .4m7.5-5 2 .4m-2.5 2.5 2 .4"/></svg>
  :kind==='handbrake'?<svg {...common}><path d="M5 21h14M11 21l3-10M12 4l2-1 3 6-3 2z"/></svg>
  :(()=>{const Icon={keypad:Grid2X2,camera:Camera,headphones:Headphones,microphone:Mic,speaker:Volume2,keyboard:Keyboard,mouse:Mouse,joystick:Joystick,gamepad:Gamepad2,unknown:Usb}[kind];return <Icon size={size} aria-hidden="true"/>;})();
 return <span className={'device-icon device-icon-'+kind} data-device-kind={kind} aria-hidden="true">{glyph}</span>;
});
