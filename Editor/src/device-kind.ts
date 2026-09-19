import type {Device} from './types.ts';
export const deviceKinds=['wheel','pedals','keypad','camera','headphones','microphone','speaker','keyboard','mouse','joystick','gamepad','handbrake','unknown'] as const;
export type DeviceKind=typeof deviceKinds[number];
// Native classification uses the original HID product/usage metadata. This
// fallback also gives remembered rules (which have only names) useful artwork.
export function deviceKind(device:Pick<Device,'name'|'detail'|'kind'>):DeviceKind{
 if(device.kind&&device.kind!=='unknown'&&deviceKinds.includes(device.kind))return device.kind;
 const name=(device.name+' '+device.detail).toLocaleLowerCase();
 if(/stream\s*deck|button\s*box|macro\s*(pad|key)/.test(name))return 'keypad';
 if(/handbrake/.test(name))return 'handbrake';
 if(/pedal|heusinkveld.*(sprint|ultimate)/.test(name))return 'pedals';
 if(/steering|wheel|simucube/.test(name))return 'wheel';
 if(/webcam|camera|\bbrio\b/.test(name))return 'camera';
 if(/headphone|headset|\barctis\b/.test(name))return 'headphones';
 if(/microphone/.test(name))return 'microphone';
 if(/speaker/.test(name))return 'speaker';
 if(/keyboard/.test(name))return 'keyboard';
 if(/mouse|trackball|touchpad/.test(name))return 'mouse';
 if(/joystick|hotas|flight\s*stick/.test(name))return 'joystick';
 if(/gamepad|xbox|dualshock|dualsense/.test(name))return 'gamepad';
 return 'unknown';
}
