import React,{memo,useEffect,useRef,useState} from 'react';
import {AppWindow,Camera,Headphones,Mic,Volume2,Keyboard,Mouse,Joystick,Gamepad2,Grid2X2,Usb,Monitor,Lightbulb,Radio,ScanBarcode} from 'lucide-react';
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
  :kind==='audio-interface'?<svg {...common}><rect x="3" y="3" width="18" height="18" rx="4"/><circle cx="12" cy="10" r="4"/><path d="M12 6v2M7 18h.01M12 18h.01M17 18h.01"/></svg>
  :kind==='capture'?<svg {...common}><path d="M5 8V4h5v4M6 4V2h3v2"/><rect x="3" y="8" width="9" height="14" rx="2"/><path d="m6 12 3 2-3 2zM16 8h5v8h-5M12 12h4"/></svg>
  :kind==='streamdeck'||kind==='streamdeck-plus'?<svg {...common}><rect x="2" y="4" width="20" height="16" rx="3"/>{[5,10,15].map(x=><React.Fragment key={x}><rect x={x} y="7" width="3" height="3" rx=".5"/>{kind==='streamdeck'?<rect x={x} y="13" width="3" height="3" rx=".5"/>:<circle cx={x+1.5} cy="15" r="1.5"/>}</React.Fragment>)}</svg>
  :kind==='shifter'?<svg {...common}><path d="M5 4v15M19 4v15M5 12h14M12 12v7"/><circle cx="12" cy="5" r="3"/><path d="M12 8v4"/></svg>
  :kind==='throttle'?<svg {...common}><path d="M3 21h18v-5H3zM8 16 6 8m10 8-2-8"/><rect x="3" y="3" width="6" height="5" rx="2"/><rect x="11" y="3" width="6" height="5" rx="2"/></svg>
  :kind==='rudder'?<svg {...common}><path d="M4 21h16M7 18l2-5m8 5-2-5M7 18h10"/><rect x="3" y="3" width="6" height="10" rx="2"/><rect x="15" y="3" width="6" height="10" rx="2"/><path d="M5 7h2m10 0h2"/></svg>
  :kind==='footswitch'?<svg {...common}><path d="M2 19h20L19 7H5zM8 7 7 19M16 7l1 12M11 11h2"/></svg>
  :kind==='haptic'?<svg {...common}><rect x="7" y="5" width="10" height="14" rx="3"/><path d="M10 9h4m-4 6h4M3 8a7 7 0 0 0 0 8m18-8a7 7 0 0 1 0 8"/></svg>
  :(()=>{const Icon={keypad:Grid2X2,camera:Camera,headphones:Headphones,microphone:Mic,speaker:Volume2,keyboard:Keyboard,mouse:Mouse,joystick:Joystick,gamepad:Gamepad2,monitor:Monitor,lighting:Lightbulb,receiver:Radio,barcode:ScanBarcode,unknown:Usb}[kind];return <Icon size={size} strokeWidth={1.6} aria-hidden="true"/>;})();
 return <span className={'device-icon device-icon-'+kind} data-device-kind={kind} aria-hidden="true">{glyph}</span>;
});
