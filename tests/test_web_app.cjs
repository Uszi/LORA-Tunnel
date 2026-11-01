// Offline DOM contract only. No browser, serial port or device is opened.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const code = fs.readFileSync(require('node:path').join(__dirname, '../web/app.js'), 'utf8');
const release = {channel:'beta',version:'0.2.2-beta.1',target:'heltec-v4-r8-tft-us915',flash_bytes:16777216,psram_bytes:8388608,factory_sha256:'a'.repeat(64),factory_file:'factory.bin'};
const manifest = {version:'0.2.2-beta.1',builds:[{chipFamily:'ESP32-S3',parts:[{offset:0,path:'../downloads/factory.bin'}]}]};
async function run(r=release,m=manifest,httpOK=true) {
  const controls = {'confirm-target':{checked:true,addEventListener(event,callback){this.change=callback;}},installer:{hidden:true},'install-status':{textContent:''}};
  vm.runInNewContext(code,{document:{getElementById:id=>controls[id]},fetch:async path=>({ok:httpOK,json:async()=>path==='release.json'?r:m})});
  await new Promise(resolve=>setImmediate(resolve));
  return controls;
}
test('valid metadata still requires explicit hardware confirmation',async()=>{
  const c=await run(); assert.equal(c['confirm-target'].checked,false); assert.equal(c.installer.hidden,true);
  c['confirm-target'].checked=true;c['confirm-target'].change();assert.equal(c.installer.hidden,false);
  c['confirm-target'].checked=false;c['confirm-target'].change();assert.equal(c.installer.hidden,true);
});
test('mismatched version never enables installer',async()=>{
  const c=await run({...release,version:'0.2.3-beta.1'});c['confirm-target'].checked=true;c['confirm-target'].change();
  assert.equal(c.installer.hidden,true);assert.match(c['install-status'].textContent,/metadata could not be verified/);
});
test('wrong target chip or offset never enables installer',async()=>{
  for(const builds of [[{chipFamily:'ESP32',parts:[{offset:0,path:'../downloads/factory.bin'}]}],[{chipFamily:'ESP32-S3',parts:[{offset:65536,path:'../downloads/factory.bin'}]}]]){
    const c=await run(release,{...manifest,builds});c['confirm-target'].checked=true;c['confirm-target'].change();assert.equal(c.installer.hidden,true);
  }
});
test('failed metadata download leaves installer unavailable',async()=>{
  const c=await run(release,manifest,false);assert.equal(c.installer.hidden,true);assert.match(c['install-status'].textContent,/unavailable/);
});
