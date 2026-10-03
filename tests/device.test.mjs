import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { encodeLayout, decodeLayout, crc32, fromBase64 } from '../dist/device-codec.mjs';
import { renderPixels, rgb565, clone } from '../dist/core.mjs';
import { layoutPreset } from '../dist/presets.mjs';
import { FlightdeckSerial, LivePreview } from '../dist/serial.mjs';
const defaults=JSON.parse(fs.readFileSync(new URL('../dist/defaults.json',import.meta.url)));
const font=JSON.parse(fs.readFileSync(new URL('../dist/font.json',import.meta.url)));
const root=new URL('../',import.meta.url).pathname;

test('compact layout roundtrip keeps fields, geometry, filters and RGB565 color precision',()=>{
  const bytes=encodeLayout(defaults), roundtrip=decodeLayout(bytes);
  assert.deepEqual(encodeLayout(roundtrip),bytes);
  assert.deepEqual(roundtrip.filters,defaults.filters);
  assert.equal(bytes.length<28000,true);
  assert.throws(()=>decodeLayout(bytes.subarray(0,bytes.length-1)));
  assert.throws(()=>decodeLayout(Uint8Array.from([...bytes,0])));
  const invalid=bytes.slice();invalid[5]=2;assert.throws(()=>decodeLayout(invalid));
  assert.equal(crc32(new TextEncoder().encode('123456789')),0xcbf43926);
});

test('native ESP32 renderer exactly matches Studio for presets, all fields, missing data and clipping',()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'flightdeck-native-'));
  try {
    const exe=path.join(dir,'render');
    execFileSync(process.env.CXX || 'g++',['-std=c++17','-Wall','-Wextra','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-I',path.join(root,'firmware/include'),path.join(root,'tests/device_native.cpp'),'-o',exe]);
    const variants=[];
    for(const preset of ['journey','large','classic','minimal','right']) {const c=clone(defaults);c.layout.elements=layoutPreset(defaults,preset);variants.push(c);}
    for(const e of defaults.layout.elements) for(const metric of [false,true]) for(const scale of [1,2,3]) {
      const c=clone(defaults);c.layout.units=metric?'metric':'aviation';
      c.layout.elements=[{...e,x:1,y:2,width:62,height:29,scale}];variants.push(c);
    }
    const custom=clone(defaults);custom.logos.UAL={width:2,height:2,pixels:[null,'#fb3162','#0012ff',null]};variants.push(custom);
    for(const [index,c] of variants.entries()) {
      const layoutFile=path.join(dir,'layout.bin'),frameFile=path.join(dir,'frame.bin');fs.writeFileSync(layoutFile,encodeLayout(c));
      for(const scenario of index<5?[0,1,2,3,4,5,6,7]:[0,1]) {
        let f={icao24:'UAL-test-id',callsign:'UAL247',airline_code:'UAL',airline_name:'United Air',departure_airport:'PHL',arrival_airport:'ORD',aircraft_type:'B787-9',altitude_ft:35000,speed_knots:430,distance_km:45,heading:90,vertical_rate_fpm:-1.5,departure_time:1700000000,arrival_time:1700043200};
        if(scenario===1) f={callsign:'N625EC',airline_code:'PVT'};
        if(scenario===2||scenario===3) f=null;
        if(scenario>=4) Object.assign(f,{departure_time:null,arrival_time:null,progress_percent:scenario===4?0:scenario===5?100:50});
        execFileSync(exe,[layoutFile,frameFile,String(scenario)]);
        const status=scenario===2||scenario===7?'stale':'fresh';
        assert.deepEqual(fs.readFileSync(frameFile),Buffer.from(rgb565(renderPixels(c,f,font,status,1700021600))),`variant ${index}, scenario ${scenario}`);
      }
    }
  } finally {fs.rmSync(dir,{recursive:true,force:true});}
});

class FakePort {
  constructor() {this.commands=[];this.saved=encodeLayout(defaults);this.badCrc=false;}
  getInfo() {return {usbVendorId:0x303a,usbProductId:0x1001};}
  async open() {
    this.readable=new ReadableStream({start:c=>this.controller=c});
    this.writable=new WritableStream({write:bytes=>this.handle(JSON.parse(new TextDecoder().decode(bytes)))});
  }
  async close() {this.closed=true;}
  reply(message) {
    const bytes=new TextEncoder().encode('boot diagnostic\n'+JSON.stringify(message)+'\n');
    this.controller.enqueue(bytes.subarray(0,9));this.controller.enqueue(bytes.subarray(9));
  }
  handle(command) {
    this.commands.push(command);const response={id:command.id,ok:true};
    switch(command.cmd) {
      case 'hello':Object.assign(response,{product:this.wrong?'other':'flightdeck',protocol:2,width:64,height:32});break;
      case 'layout_info':Object.assign(response,{length:this.saved.length,crc:crc32(this.saved)});break;
      case 'layout_chunk':response.data=Buffer.from(this.saved.subarray(command.offset,command.offset+384)).toString('base64');break;
      case 'upload_begin':this.upload={bytes:[],...command};break;
      case 'upload_chunk':assert.equal(command.offset,this.upload.bytes.length);this.upload.bytes.push(...fromBase64(command.data));break;
      case 'upload_commit':
        if(this.badCrc) {response.ok=false;response.error='checksum mismatch';}
        else {assert.equal(this.upload.length,this.upload.bytes.length);assert.equal(this.upload.crc,crc32(this.upload.bytes));if(this.upload.kind==='layout') this.saved=Uint8Array.from(this.upload.bytes);}
        break;
    }
    this.reply(response);
  }
}
test('USB handshake, fragmented replies, bounded upload and readback work without a service',async()=>{
  const port=new FakePort(),device=new FlightdeckSerial();await device.open(port);
  const c=clone(defaults);c.layout.brightness=17;const bytes=encodeLayout(c);
  await device.upload('layout',bytes);assert.deepEqual(await device.downloadLayout(),bytes);
  assert.ok(port.commands.filter(c=>c.cmd==='upload_chunk').every(c=>c.data.length<=512));
  await device.close();assert.equal(device.connected,false);assert.equal(port.closed,true);
});
test('wrong firmware is rejected before settings can be sent',async()=>{
  const port=new FakePort();port.wrong=true;const device=new FlightdeckSerial();
  await assert.rejects(device.open(port),/not running Flightdeck/);
  assert.deepEqual(port.commands.map(c=>c.cmd),['hello']);assert.equal(device.connected,false);
});
test('rejected upload aborts and keeps the previous saved layout',async()=>{
  const port=new FakePort(),device=new FlightdeckSerial();await device.open(port);port.badCrc=true;
  const c=clone(defaults);c.layout.brightness=80;
  await assert.rejects(device.upload('layout',encodeLayout(c)),/checksum mismatch/);
  assert.deepEqual(port.saved,encodeLayout(defaults));assert.equal(port.commands.at(-1).cmd,'upload_abort');await device.close();
});
test('preview applies backpressure, sends the latest frame and resumes autonomous mode',async()=>{
  let release;const calls=[];
  const device={connected:true,upload:async(kind,bytes)=>{calls.push(bytes[1]);if(calls.length===1) await new Promise(r=>release=r);},command:async cmd=>calls.push(cmd)};
  const preview=new LivePreview(device);preview.enabled=true;
  for(let i=1;i<10;i++) preview.submit(new Uint8Array(4096).fill(i),25);
  assert.deepEqual(calls,[1]);release();await new Promise(r=>setTimeout(r,0));assert.deepEqual(calls,[1,9]);
  await preview.stop();assert.equal(calls.at(-1),'resume');
});
