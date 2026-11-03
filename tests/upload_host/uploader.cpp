#include <Arduino.h>
#include <iostream>
#include "../../src/uploader.cpp"
uint32_t hostMillis=1000;
namespace host {
void(*task)(void*)=nullptr;void* taskArg=nullptr;
std::map<std::string,std::string> files;std::vector<std::string> directories;
std::string failWritePath;bool connected=true;unsigned associations=0;uint32_t epoch=1;
std::vector<HttpScript> scripts;std::vector<HttpCall> calls;void(*afterWrite)()=nullptr;
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
  config=Config{};strlcpy(config.wifi_name,"Synthetic home",128);strlcpy(config.wifi_password,"testpass",128);strlcpy(config.wigle_name,"testname",128);strlcpy(config.wigle_token,"testtoken",128);strlcpy(config.wardrift_token,"testwardrift",128);strlcpy(config.wdgwars_token,"testwdgwars",128);
  config.auto_wigle=service==0;config.auto_wardrift=service==1;config.auto_wdgwars=service==2;
  busy=false;cancelled=false;requests=retryMask=authBlocked=0;nextRetry=0;associationFailures=0;wasIdle=false;initialPending=true;settingsEpoch=1;
  uploaderBegin();host::files[sourcePath]=session();
}
void task(){if(host::task){auto fn=host::task;auto arg=host::taskArg;host::task=nullptr;try{fn(arg);}catch(const HostTaskFinished&){} }expect(!tunnel::uploaderBusy(),"worker releases busy state");}
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

  reset();host::files[sourcePath]=session(10000);automatic();expect(host::calls.size()>1,"large session split into bounded chunks");for(const auto& chunk:host::calls)expect(chunk.body.size()<=upload::CsvChunkLimit+400,"each multipart CSV chunk stays below 1 MiB plus envelope");const auto count=host::calls.size();restart();automatic();expect(host::calls.size()==count,"all accepted chunk checkpoints survive restart");
  std::cout<<"Uploader host: "<<checks<<" checks passed against real worker with mocked SD/TLS transport. No network requests made.\n";
}
