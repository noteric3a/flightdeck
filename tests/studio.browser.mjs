// Optional integration check: npm install --no-save playwright; npx playwright install chromium
// Run after python scripts/studio.py --no-browser: STUDIO_URL=http://localhost:8765 node tests/studio.browser.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import { encodeLayout, decodeLayout } from '../dist/device-codec.mjs';
const require=createRequire(import.meta.url);
const {chromium}=require('playwright');
const defaults=JSON.parse(fs.readFileSync(new URL('../dist/defaults.json',import.meta.url)));
const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
const page=await browser.newPage({viewport:{width:1440,height:1000}});
const errors=[],requests=[];
page.on('pageerror',e=>errors.push(e.message));
page.on('request',r=>requests.push(r.url()));
await page.addInitScript(({layout})=>{
  const crc32=bytes=>{let crc=0xffffffff;for(const b of bytes){crc^=b;for(let i=0;i<8;i++)crc=(crc>>>1)^(0xedb88320&-(crc&1));}return (crc^0xffffffff)>>>0;};
  const mock={saved:Uint8Array.from(layout),mode:'demo',ssid:'',has_api_key:false,has_password:false,commands:[],frames:[],preview:false};
  const info=()=>({product:'flightdeck',protocol:2,width:64,height:32,clock:Date.now()/1000,panel_ready:true,storage_ready:true,mode:mock.mode,ssid:mock.ssid,has_api_key:mock.has_api_key,has_password:mock.has_password,poll_seconds:300,max_per_hour:24,requests_this_hour:0,wifi_connected:!!mock.ssid,ip:mock.ssid?'192.0.2.1':'',message:mock.mode==='demo'?'Demo mode':'Live FlightAware',preview:mock.preview,stale:false,flights:[{icao24:'UAL-test-id',callsign:'UAL247',airline_code:'UAL',airline_name:'United',latitude:40.5,longitude:-86.8,altitude_ft:35000,speed_knots:430,distance_km:15,heading:90,vertical_rate_fpm:null,on_ground:false,position_time:Date.now()/1000,departure_airport:'PHL',arrival_airport:'ORD',aircraft_type:'B787-9',departure_time:Date.now()/1000-3600,arrival_time:Date.now()/1000+3600,details_source:'sample'}]});
  const port={
    getInfo:()=>({usbVendorId:0x303a,usbProductId:0x1001}),
    async open(){
      this.readable=new ReadableStream({start:c=>this.controller=c});
      this.writable=new WritableStream({write:async bytes=>{
        const c=JSON.parse(new TextDecoder().decode(bytes));mock.commands.push(c.cmd);let r={id:c.id,ok:true};
        if(c.cmd==='hello'||c.cmd==='status')Object.assign(r,info());
        if(c.cmd==='layout_info')Object.assign(r,{length:mock.saved.length,crc:crc32(mock.saved)});
        if(c.cmd==='layout_chunk')r.data=btoa(String.fromCharCode(...mock.saved.slice(c.offset,c.offset+384)));
        if(c.cmd==='configure') {Object.assign(mock,{mode:c.mode,ssid:c.ssid});if(c.api_key)mock.has_api_key=true;if(c.password)mock.has_password=true;Object.assign(r,info());}
        if(c.cmd==='upload_begin')mock.upload={...c,bytes:[]};
        if(c.cmd==='upload_chunk')mock.upload.bytes.push(...Uint8Array.from(atob(c.data),x=>x.charCodeAt(0)));
        if(c.cmd==='upload_commit') {
          if(crc32(mock.upload.bytes)!==mock.upload.crc)throw new Error('Test observed corrupt USB upload');
          if(mock.upload.kind==='frame'){mock.frames.push(crc32(mock.upload.bytes));mock.preview=true;}
          else {mock.saved=Uint8Array.from(mock.upload.bytes);mock.preview=false;}
        }
        if(c.cmd==='resume')mock.preview=false;
        this.controller.enqueue(new TextEncoder().encode(JSON.stringify(r)+'\n'));
      }});
    },async close(){},
  };
  const serial=new EventTarget();serial.requestPort=async()=>port;serial.getPorts=async()=>[port];
  Object.defineProperty(navigator,'serial',{value:serial,configurable:true});window.usbMock=mock;
},{layout:Array.from(encodeLayout(defaults))});
try {
  await page.goto(process.env.STUDIO_URL||'http://localhost:8765');
  await page.waitForFunction(()=>document.querySelectorAll('#layers .layer-row').length===12);
  await page.click('#connection-open');await page.waitForSelector('#connection-dialog[open]');
  await page.fill('#wifi-ssid','Flightdeck test network');await page.fill('#wifi-password','dummy-wifi-password');await page.fill('#aeroapi-key','dummy-api-key');await page.selectOption('#device-mode','live');
  await page.click('#connection-form button[type=submit]');await page.waitForFunction(()=>!document.querySelector('#connection-dialog').open);
  assert.equal(await page.inputValue('#aeroapi-key'),'');assert.equal(await page.inputValue('#wifi-password'),'');
  assert.equal(await page.evaluate(()=>JSON.stringify(localStorage).includes('dummy-')),false);
  await page.check('#live-preview');await page.waitForFunction(()=>window.usbMock.frames.length>0);
  await page.fill('#element-x','5');await page.locator('#element-x').dispatchEvent('change');
  await page.waitForFunction(()=>new Set(window.usbMock.frames).size>1);
  await page.click('#save');await page.waitForFunction(()=>document.querySelector('#save').textContent==='Saved to ESP32');
  const saved=decodeLayout(Uint8Array.from(await page.evaluate(()=>Array.from(window.usbMock.saved))));assert.equal(saved.layout.elements[0].x,5);
  assert.equal(await page.isChecked('#live-preview'),false);
  await page.click('#connection-open');await page.screenshot({path:'/tmp/flightdeck-studio-settings.png',fullPage:true});
  await page.click('#disconnect');await page.waitForFunction(()=>document.querySelector('#connection-open').textContent==='Connect ESP32');
  await page.click('#connection-open');await page.waitForSelector('#connection-dialog[open]');
  assert.equal(await page.inputValue('#wifi-ssid'),'Flightdeck test network');
  await page.click('#connection-close');await page.screenshot({path:'/tmp/flightdeck-studio-editor.png',fullPage:true});
  assert.deepEqual(errors,[]);
  assert.equal(requests.some(url=>/\/api\//.test(url)),false);
  console.log('Studio browser integration passed: USB connect/reconnect, provisioning, secret clearing, live edit, persisted layout, no backend API calls.');
} finally {await browser.close();}
