#include <Arduino.h>
#include <iostream>
#include <ArduinoJson.h>
#include <atomic>
#include <new>
#include <ctime>
void* uploadHostMalloc(size_t);
void uploadHostFree(void*);
#define malloc uploadHostMalloc
#define free uploadHostFree
#include "../../src/uploader.cpp"
#undef malloc
#undef free
uint32_t hostMillis=1000;
namespace host {
void(*task)(void*)=nullptr;void* taskArg=nullptr;
bool createTask=true,bleReady=true,pauseOK=true,restoreOK=true,disconnected=true;
unsigned pauses=0,restores=0,activeHttp=0,openFiles=0,allocation=0,failAllocation=0;
unsigned regularFiles=0,peakRegularFiles=0,bufferCalls=0,failBufferCall=0,checkpointCancels=0;
void(*afterPause)()=nullptr;
std::set<void*> buffers;
std::map<std::string,std::string> files;std::vector<std::string> directories;
std::string failWritePath;bool connected=true;unsigned associations=0;uint32_t epoch=1;
std::vector<HttpScript> scripts;std::vector<HttpCall> calls;void(*afterWrite)()=nullptr;
}
void* uploadHostMalloc(size_t size){
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  if(host::bleReady)throw std::runtime_error("Upload buffer allocated before BLE release");
#endif
  if(++host::allocation==host::failAllocation)return nullptr;
  void* p=std::malloc(size);if(p)host::buffers.insert(p);return p;
}
void uploadHostFree(void* p){host::buffers.erase(p);std::free(p);}
namespace tunnel {
bool scannersPauseBleForUpload(){
  if(!uploaderBusy())throw std::runtime_error("BLE released without upload ownership");
  ++host::pauses;host::bleReady=false;
  if(host::afterPause){auto fn=host::afterPause;host::afterPause=nullptr;fn();}
  return host::pauseOK;
}
bool scannersRestoreBleAfterUpload(){
  if(!uploaderBusy()||host::activeHttp||host::openFiles||!host::buffers.empty()||!host::disconnected)
    throw std::runtime_error("BLE restored before worker cleanup or after ownership release");
  ++host::restores;host::bleReady=host::restoreOK;return host::restoreOK;
}
bool scannersCaptureReady(){return host::bleReady;}
}
SDClass SD;WiFiClass WiFi;
namespace tunnel {Config config;int busMutex=0;}
namespace {
unsigned checks=0;
void expect(bool ok,const char* description){++checks;if(!ok){std::cerr<<"FAIL "<<description<<"\n";std::exit(1);}}
const char* sourcePath="/sessions/LT-12345678.jsonl";
const char* observation=R"({"schema":1,"capture_mode":"wardriver","type":"wifi","utc":"2026-11-02T14:32:10Z","gps":{"valid":true,"age_ms":500,"lat":0,"lon":0,"alt_m":45},"wifi":{"bssid":"aa:bb:cc:dd:ee:ff","ssid":"Synthetic AP","security":"[WPA2]","rssi":-52,"channel":6,"frequency_mhz":2437}})";
std::string session(unsigned rows=1,const char* mode="wardriver",bool closed=true) {
  std::string text="{\"schema\":1,\"type\":\"session_start\",\"session\":\"12345678\",\"capture_mode\":\"";text+=mode;text+="\"}\n";
  for(unsigned i=0;i<rows;++i){text+=observation;text+='\n';}
  if(closed){text+="{\"schema\":1,\"type\":\"session_end\",\"session\":\"12345678\",\"capture_mode\":\"";text+=mode;text+="\"}\n";}
  return text;
}
void reset(int service=0) {
  using namespace tunnel;
  host::files.clear();host::directories={"/sessions"};host::failWritePath.clear();host::scripts.clear();host::calls.clear();host::connected=true;host::associations=0;host::afterWrite=nullptr;host::epoch=1;hostMillis=1000;
  if(!host::buffers.empty()||host::activeHttp||host::openFiles)throw std::runtime_error("Previous worker leaked ownership/resources");
  host::createTask=host::bleReady=host::pauseOK=host::restoreOK=host::disconnected=true;
  host::peakRegularFiles=host::bufferCalls=host::failBufferCall=host::checkpointCancels=0;host::fileHooks().afterFlush=nullptr;
  host::pauses=host::restores=host::allocation=host::failAllocation=0;host::afterPause=nullptr;
  config=Config{};strlcpy(config.wifi_name,"Synthetic home",128);strlcpy(config.wifi_password,"testpass",128);strlcpy(config.wigle_name,"testname",128);strlcpy(config.wigle_token,"testtoken",128);strlcpy(config.wardrift_token,"testwardrift",128);strlcpy(config.wdgwars_token,"testwdgwars",128);
  config.auto_wigle=service==0;config.auto_wardrift=service==1;config.auto_wdgwars=service==2;
  busy=false;cancelled=false;requests=retryMask=authBlocked=0;nextRetry=0;associationFailures=0;wasIdle=false;initialPending=true;settingsEpoch=1;
  uploaderBegin();host::files[sourcePath]=session();
}
void task(){
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  const auto before=host::pauses,restored=host::restores;
#endif
  if(host::task){auto fn=host::task;auto arg=host::taskArg;host::task=nullptr;try{fn(arg);}catch(const HostTaskFinished&){} }
  expect(!tunnel::uploaderBusy(),"worker releases busy state");
  expect(host::buffers.empty()&&host::activeHttp==0&&host::openFiles==0,"all worker buffers, HTTP and SD files released");
  expect(host::regularFiles==0&&host::peakRegularFiles<=2,"all uploader paths fit two regular SD file slots");
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  if(host::pauses>before&&host::pauseOK)expect(host::restores==restored+1,"every released BLE path restores before completion");
#else
  expect(host::pauses==0&&host::restores==0,"Heltec BLE lifetime is unchanged");
#endif
}
void automatic(){tunnel::uploaderTick(true,true);task();}
void restart(){tunnel::authBlocked=0;tunnel::wasIdle=false;tunnel::uploaderBegin();}
void manual(int service){expect(tunnel::uploaderRequest(service),"manual request accepted");tunnel::uploaderTick(true,false);task();}
bool metadataHas(const char* value){for(const auto& item:host::files)if(item.first.find(".json")!=std::string::npos&&item.second.find(value)!=std::string::npos)return true;return false;}
}
int main() {
  using namespace tunnel;
  reset();uploaderTick(false);expect(host::task==nullptr && host::associations==0,"capture never starts network");uploaderTick(true,false);expect(host::task==nullptr,"editing suppresses automatic network");
  manual(0);expect(host::calls.size()==1,"manual upload works while Uploads page blocks automatic");
  const auto& call=host::calls.front();expect(call.config.crt_bundle_attach!=nullptr && call.config.disable_auto_redirect,"verified TLS and redirects disabled");expect(call.config.timeout_ms==4000,"socket timeout bounded");
  expect(call.promised==int(call.body.size()),"multipart content length matches streamed bytes");expect(call.body.find("WigleWifi-1.6")!=std::string::npos&&call.body.find("Synthetic AP")!=std::string::npos,"eligible CSV streamed");
  expect(call.headers.at("Authorization")=="Basic dGVzdG5hbWU6dGVzdHRva2Vu","WiGLE Basic header encoded correctly");
  expect(metadataHas("\"state\":\"accepted\""),"receipt persisted");const auto original=host::files[sourcePath];restart();automatic();expect(host::calls.size()==1,"restart never resends accepted chunk");expect(host::files[sourcePath]==original,"native log unchanged");

  reset();host::files[sourcePath]=session(1,"wardriver",false);automatic();expect(host::calls.empty(),"open session is not uploaded");
  reset();host::files[sourcePath]=session(1,"recon");automatic();expect(host::calls.empty(),"Recon file is not uploaded");
  reset();host::files[sourcePath].insert(1,"\"synthetic\":true,");automatic();expect(host::calls.empty(),"synthetic session remains offline");
  reset();const auto modeAt=host::files[sourcePath].find("\"wardriver\"");host::files[sourcePath].replace(modeAt,11,"null");automatic();expect(host::calls.empty(),"explicit null session mode is not legacy Wardriver");
  reset();host::files[sourcePath]=session(0);automatic();expect(host::calls.empty()&&metadataHas("\"state\":\"skipped\""),"empty export marked skipped locally");

  reset(1);host::scripts.push_back({200,"{\"accepted\":true}",1,false,false});automatic();expect(host::calls.size()==1&&metadataHas("accepted"),"fragmented Wardrift receipt accepted");expect(host::calls.front().headers.at("Content-Type")=="text/csv","Wardrift raw CSV content type");
  reset(1);host::scripts.push_back({409,"{\"duplicate\":true}",7,false,false});automatic();restart();automatic();expect(host::calls.size()==1,"409 is rejected and not automatically resent");
  reset(1);host::scripts.push_back({200,"<html>not a receipt</html>",7,false,false});automatic();expect(metadataHas("\"state\":\"rejected\""),"HTML response cannot mark upload accepted");
  reset(1);host::scripts.push_back({200,std::string(3000,' ')+"{\"accepted\":true}",7,false,false});automatic();expect(metadataHas("\"state\":\"rejected\""),"oversize/truncated response rejected");

  reset(2);host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});automatic();expect(metadataHas("\"job\":\"42\""),"numeric async job persisted canonically");
  restart();host::scripts.push_back({200,"{\"status\":\"done\"}",2,false,false});automatic();expect(host::calls.size()==2&&host::calls[1].config.method==HTTP_METHOD_GET&&host::calls[1].url.find("/42")!=std::string::npos,"restart polls job instead of reposting");
  expect(metadataHas("\"state\":\"accepted\""),"job completion persists success");
  reset(2);host::scripts.push_back({202,"{\"job_id\":\"job-1\"}",7,false,false});automatic();hostMillis+=30001;host::scripts.push_back({200,"{\"status\":\"failed\"}",7,false,false});automatic();expect(metadataHas("\"state\":\"rejected\""),"terminal job failure persisted");host::scripts.push_back({202,"{\"job_id\":43}",7,false,false});manual(2);expect(host::calls.back().config.method==HTTP_METHOD_POST,"manual retry resubmits terminal failed job");

  reset();host::scripts.push_back({401,"{}",7,false,false});automatic();restart();automatic();expect(host::calls.size()==1,"authentication pause survives reboot");strlcpy(config.wigle_token,"replacement",128);uploaderConfigChanged();automatic();expect(host::calls.size()==2,"credential change unblocks authentication pause");
  reset();host::scripts.push_back({0,"",7,true,false});automatic();expect(metadataHas("\"state\":\"uncertain\""),"lost POST receipt remains uncertain");restart();automatic();expect(host::calls.size()==1,"uncertain receipt not resent automatically");manual(0);expect(host::calls.size()==2,"explicit retry permits ambiguous request");
  reset();host::scripts.push_back({0,"",7,false,true});automatic();expect(metadataHas("\"state\":\"retry\""),"failed TLS/open before body is safe to retry");hostMillis+=30001;automatic();expect(host::calls.size()==2,"safe retry runs after backoff");
  reset();for(int i=0;i<4;++i)host::scripts.push_back({503,"{}",7,false,false});automatic();hostMillis+=30001;automatic();hostMillis+=120001;automatic();hostMillis+=600001;automatic();expect(host::calls.size()==3,"per-source service retries stop after three failures");
  manual(0);expect(host::calls.size()==4,"explicit manual request resets exhausted per-file budget");

  reset();host::scripts.push_back({0,"",7,true,false});automatic();for(auto& item:host::files)if(item.first.find("/0-0.")!=std::string::npos)item.second="{partial";restart();automatic();expect(host::calls.size()==1,"corrupt existing checkpoint fails closed");

  reset();host::failWritePath=".json";automatic();expect(host::calls.empty(),"SD checkpoint failure prevents network send");
  reset();host::afterWrite=[](){tunnel::uploaderCancel();};automatic();expect(metadataHas("\"state\":\"uncertain\""),"cancellation after send preserves ambiguity");expect(nextRetry==0&&retryMask==0,"cancel does not schedule itself again");

  reset();expect(uploaderRequest(0),"manual request starts while automatic scheduling is hidden");
  uploaderTick(true,false);uploaderCancel();task();hostMillis+=30001;uploaderTick(true,true);
  expect(host::task==nullptr&&!uploaderBusy(),"leaving Uploads after manual cancellation cannot trigger a fresh automatic worker");
  uploaderTick(false,true);uploaderTick(true,true);
  expect(host::task!=nullptr&&uploaderBusy(),"a later genuine capture-stop transition restores automatic eligibility");task();
  expect(host::calls.size()==1,"next completed capture interval can upload after earlier cancellation");

  // Cancel after requestHttp's stop check, during the result checkpoint,
  // just before processChunk can re-arm a retry or asynchronous job poll.
  for(int service: {0,1,2}) {
    reset(service);
    host::scripts.push_back(service==2?host::HttpScript{202,"{\"job_id\":42}",3,false,false}:host::HttpScript{503,"{}",7,false,false});
    host::fileHooks().afterFlush=[](const std::string& path){
      const auto& text=host::files[path];
      if(text.find("\"state\":\"retry\"")!=std::string::npos || text.find("\"state\":\"job\"")!=std::string::npos) {
        expect(!host::calls.empty()&&host::activeHttp==0,"cancel injected after completed HTTP at result checkpoint boundary");
        host::fileHooks().afterFlush=nullptr;++host::checkpointCancels;uploaderCancel();
      }
    };
    automatic();
    expect(host::checkpointCancels==1,"result checkpoint cancellation hook fired exactly once");
    expect(nextRetry==0&&retryMask==0,"cancelled checkpoint cannot re-arm automatic retry or job polling");
    expect(metadataHas(service==2?"\"state\":\"job\"":"\"state\":\"retry\""),"cancellation retains the truthful persisted receipt state");
    const auto callsBefore=host::calls.size();hostMillis+=30001;uploaderTick(true,true);
    expect(host::task==nullptr&&!uploaderBusy()&&host::calls.size()==callsBefore,"no automatic worker or request thirty seconds after cancellation");
  }

  reset();host::files[sourcePath]=session(10000);automatic();expect(host::calls.size()>1,"large session split into bounded chunks");for(const auto& chunk:host::calls)expect(chunk.body.size()<=upload::CsvChunkLimit+400,"each multipart CSV chunk stays below 1 MiB plus envelope");const auto count=host::calls.size();restart();automatic();expect(host::calls.size()==count,"all accepted chunk checkpoints survive restart");
  reset();host::connected=false;automatic();expect(host::calls.empty(),"association failure sends no HTTP");
  reset();host::failAllocation=1;automatic();expect(host::calls.empty(),"line allocation failure sends no HTTP");
  reset();host::failAllocation=2;automatic();expect(host::calls.empty(),"row allocation failure sends no HTTP");
  reset();host::createTask=false;uploaderTick(true,true);expect(!uploaderBusy()&&!host::task&&host::calls.empty()&&host::pauses==0,"task creation failure never releases BLE or sends HTTP");
  reset();uploaderTick(true,true);uploaderCancel();task();expect(host::pauses==0&&host::calls.empty(),"cancel before worker entry never starts BLE transition or HTTP");
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  reset();host::failBufferCall=1;automatic();expect(host::calls.empty()&&host::regularFiles==0,"stdio buffer failure closes file and prevents sending its contents");
  reset();automatic();const auto opens=host::bufferCalls;
  for(unsigned fail=2;fail<=opens;++fail){reset();host::failBufferCall=fail;automatic();expect(host::regularFiles==0,"every stdio buffer failure releases SD file slots");}
  reset();host::pauseOK=false;automatic();expect(host::calls.empty()&&host::associations==0&&host::restores==0&&!scannersCaptureReady(),"failed BLE pause prevents allocations/network and keeps capture gated");
  expect(std::string(statusText).find("BLE pause failed")!=std::string::npos,"BLE pause failure remains visible");
  reset();host::restoreOK=false;automatic();expect(host::calls.size()==1&&!scannersCaptureReady(),"failed restoration does not change accepted receipt but blocks capture");
  expect(std::string(statusText).find("BLE restore failed")!=std::string::npos,"restoration failure remains visible");
  reset();host::afterPause=[](){uploaderCancel();};automatic();expect(host::calls.empty()&&host::restores==1&&scannersCaptureReady(),"cancellation during BLE pause restores without sending");
  reset();host::afterWrite=[](){uploaderCancel();};host::restoreOK=false;automatic();expect(!scannersCaptureReady()&&metadataHas("uncertain"),"cancel plus restore failure preserves uncertain receipt and capture gate");
#endif
  std::cout<<"Uploader host: "<<checks<<" checks passed against real worker with mocked SD/TLS transport. No network requests made.\n";
}
