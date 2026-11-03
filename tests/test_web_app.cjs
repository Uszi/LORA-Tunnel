// Offline DOM contract. No serial port is opened.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const code = fs.readFileSync(require('node:path').join(__dirname, '../web/app.js'), 'utf8');
const heltec = {channel:'beta',version:'0.3.0-beta.1',target:'heltec-v4-r8-tft-high-band',name:'Heltec',
  flash_bytes:16777216,psram_bytes:8388608,factory_sha256:'a'.repeat(64),factory_file:'heltec.bin',
  source_file:'heltec-source.zip',sd_file:'heltec-sd.zip',public_enabled:true,
  manifest:'firmware/heltec-v4-r8-tft-high-band/manifest.json',guide:'targets/heltec-v4-r8-tft-high-band/guides/USER_GUIDE.html',
  relink_url:'https://github.com/valleytechsolutions/LORA-Tunnel/releases/download/v0.3.0-beta.1/relink.zip'};
const card = {...heltec,version:'0.4.0-beta.1',target:'cardputer-adv-lora1262',name:'Cardputer',
  flash_bytes:8388608,psram_bytes:0,factory_file:'card.bin',app_file:'card-app.bin',app_sha256:'b'.repeat(64),
  public_enabled:false,manifest:'firmware/cardputer-adv-lora1262/manifest.json',guide:'targets/cardputer-adv-lora1262/guides/CARDPUTER_ADV.html'};
const manifest = r => ({version:r.version,builds:[{chipFamily:'ESP32-S3',parts:[{offset:0,path:'../../downloads/'+r.factory_file}]}]});
const settle = () => new Promise(resolve=>setImmediate(resolve));
async function run({targets=[heltec,card],local=false,host='example.test',search='',alter=x=>x,httpOK=true}={}) {
  const controls = {};
  const ids = ['target-select','confirm-target','installer','install-status','download-factory','download-app','target-name',
    'target-version','target-memory','confirm-label','target-guide','download-source','download-sd','factory-sha',
    'download-relink','target-state','install-button'];
  for(const id of ids) controls[id]={checked:true,hidden:true,textContent:'',href:'',value:heltec.target,
    addEventListener(event,fn){this[event]=fn;},setAttribute(key,value){this[key]=value;}};
  vm.runInNewContext(code,{document:{getElementById:id=>controls[id]},URL,URLSearchParams,
    location:{hostname:host,search},fetch:async path=>({ok:httpOK,json:async()=>path==='catalog.json'
      ?{schema:2,local_preview:local,targets}
      :alter(manifest(targets.find(r=>r.manifest===path)))})});
  await settle();
  controls.choose = async target => {controls['target-select'].value=target;await controls['target-select'].change();};
  controls.confirm = () => {controls['confirm-target'].checked=true;controls['confirm-target'].change();};
  return controls;
}
test('Heltec requires confirmation and keeps its published version',async()=>{
  const c=await run();assert.equal(c['confirm-target'].checked,false);assert.equal(c.installer.hidden,true);
  c.confirm();assert.equal(c.installer.hidden,false);assert.equal(c['target-version'].textContent,'0.3.0-beta.1');
});
test('board change resets confirmation and installs the selected manifest',async()=>{
  const c=await run({targets:[heltec,{...card,public_enabled:true}]});c.confirm();
  await c.choose(card.target);assert.equal(c['confirm-target'].checked,false);assert.equal(c.installer.hidden,true);
  c.confirm();assert.equal(c.installer.hidden,false);assert.equal(c['install-button'].manifest,card.manifest);
  assert.equal(c['download-app'].href,'downloads/card-app.bin');
});
test('pending Cardputer stays unavailable on public hosts even with preview query',async()=>{
  const c=await run({local:true,search:'?acceptance=1'});await c.choose(card.target);c.confirm();
  assert.equal(c.installer.hidden,true);assert.equal(c['download-app'].hidden,true);
  assert.match(c['install-status'].textContent,/physical hardware acceptance/);
});
test('pending Cardputer requires BOTH local preview build and explicit loopback acceptance',async()=>{
  for(const config of [{local:true,host:'localhost',search:''},{local:false,host:'localhost',search:'?acceptance=1'}]){
    const c=await run(config);await c.choose(card.target);c.confirm();assert.equal(c.installer.hidden,true);
  }
  const c=await run({local:true,host:'localhost',search:'?acceptance=1'});await c.choose(card.target);c.confirm();
  assert.equal(c.installer.hidden,false);
});
test('wrong board memory, chip, version, offset and file cannot enable installer',async()=>{
  for(const change of [m=>({...m,version:'wrong'}),m=>({...m,builds:[{chipFamily:'ESP32',parts:m.builds[0].parts}]}),
    m=>({...m,builds:[{chipFamily:'ESP32-S3',parts:[{offset:65536,path:m.builds[0].parts[0].path}]}]}),
    m=>({...m,builds:[{chipFamily:'ESP32-S3',parts:[{offset:0,path:'../../downloads/card.bin'}]}]})]){
    const c=await run({alter:change});c.confirm();assert.equal(c.installer.hidden,true);
  }
  const c=await run({targets:[{...heltec,flash_bytes:8388608},card]});c.confirm();assert.equal(c.installer.hidden,true);
});
test('failed fetch or duplicate/unknown targets fail closed',async()=>{
  for(const config of [{httpOK:false},{targets:[heltec,heltec]},{targets:[heltec,{...card,target:'unknown'}]}]){
    const c=await run(config);c.confirm();assert.equal(c.installer.hidden,true);
    assert.match(c['install-status'].textContent,/unavailable/);
  }
});
