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
void(*workerTask)(void*)=nullptr;void* workerArg=nullptr;
TaskHandle_t currentTask=reinterpret_cast<void*>(1);
bool parked=false,maintenanceOK=true,deferPark=false,parkDeferred=false;unsigned workerStack=0,created=0,deleted=0,parkWaits=0;
void(*afterPark)()=nullptr;void(*afterRestore)()=nullptr;void(*actualComplete)()=nullptr;
bool createTask=true,bleReady=true,pauseOK=true,restoreOK=true,disconnected=true;
unsigned pauses=0,restores=0,activeHttp=0,openFiles=0,allocation=0,failAllocation=0;
unsigned regularFiles=0,peakRegularFiles=0,bufferCalls=0,failBufferCall=0,checkpointCancels=0;
void(*afterPause)()=nullptr;
std::set<void*> buffers;
std::map<std::string,std::string> files;std::vector<std::string> directories;
std::string failWritePath;bool connected=true;unsigned associations=0;uint32_t epoch=1;
std::vector<HttpScript> scripts;std::vector<HttpCall> calls;void(*afterWrite)()=nullptr;
}
#include "checked_backend.h"
namespace writerHost {
int queue=1;unsigned queued=0;
TaskHandle_t writerTask=reinterpret_cast<void*>(2);
std::atomic<bool> openSession{false},busy{false},capturing{false};
std::atomic<unsigned> maintenanceState{0};
void (*maintenanceRun)(void*)=nullptr;void* maintenanceContext=nullptr;void (*maintenanceComplete)()=nullptr;
unsigned uxQueueMessagesWaiting(int){return queued;}
constexpr int pdTRUE=1;
enum class Kind{Wifi,Ble,LoRa,Start,Stop};struct Observation{Kind kind;};
struct {std::atomic<unsigned> log_drops{0};} counters;
int xQueueSend(int,const Observation*,int){++queued;return pdTRUE;}
#include "logger_maintenance_actual.inc"
}
void writerEntry(void*) {
  const auto previous=host::currentTask;host::currentTask=writerHost::writerTask;
  writerHost::serviceIdleMaintenance();host::currentTask=previous;
}
void maintenanceCompleted() {
  if(!writerHost::loggerIdle()||writerHost::loggerMaintenanceQuiescent()||!tunnel::uploaderBusy()||host::workerStack)
    throw std::runtime_error("completion must release logger maintenance before uploader ownership");
  host::actualComplete();
  if(tunnel::uploaderBusy())throw std::runtime_error("completion did not release upload ownership");
}
namespace tunnel {
bool loggerRequestMaintenance(void (*run)(void*),void* context,void (*complete)()) {
  host::actualComplete=complete;
  const bool accepted=host::maintenanceOK&&writerHost::loggerRequestMaintenance(run,context,maintenanceCompleted);
  if(accepted){host::task=writerEntry;host::taskArg=nullptr;}
  return accepted;
}
bool loggerMaintenanceQuiescent(){return writerHost::loggerMaintenanceQuiescent();}
bool loggerIdle(){return writerHost::loggerIdle();}
}
TaskHandle_t xTaskGetCurrentTaskHandle(){return host::currentTask;}
eTaskState eTaskGetState(TaskHandle_t task){if(task!=reinterpret_cast<void*>(3))throw std::runtime_error("invalid worker handle");return host::parked?eSuspended:eReady;}
void vTaskDelay(uint32_t n){
  hostMillis+=n;
  if(host::parkDeferred) {
    if(host::currentTask!=writerHost::writerTask||!tunnel::uploaderBusy()||host::workerStack!=16384||host::bleReady)
      throw std::runtime_error("completion publication released resources before suspension");
    ++host::parkWaits;host::parked=true;host::parkDeferred=false;
  }
  if(host::workerTask) {
    auto fn=host::workerTask;auto arg=host::workerArg;host::workerTask=nullptr;
    const auto previous=host::currentTask;host::currentTask=reinterpret_cast<void*>(3);
    try{fn(arg);}catch(const HostTaskSuspended&){}host::currentTask=previous;
    if(host::afterPark){auto callback=host::afterPark;host::afterPark=nullptr;callback();}
  }
}
void vTaskSuspend(void* task){
  if(task||host::currentTask!=reinterpret_cast<void*>(3)||!tunnel::uploaderBusy()||!host::buffers.empty()||host::activeHttp||host::openFiles)
    throw std::runtime_error("worker parked before cleanup or ownership lost");
  if(host::deferPark){host::deferPark=false;host::parkDeferred=true;}
  else host::parked=true;
  throw HostTaskSuspended{};
}
void vTaskDelete(void* task){
  if(!task) {
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
    // A real self-delete retains the stack until the idle task runs; a fixture
    // must never pretend it freed memory before the BLE restore boundary.
    if(host::workerStack)throw std::runtime_error("Card worker self-delete leaves deferred stack reclamation");
#endif
    throw HostTaskFinished{};
  }
  if(task!=reinterpret_cast<void*>(3)||!host::parked||host::currentTask!=writerHost::writerTask||!host::workerStack||host::bleReady||!tunnel::uploaderBusy())
    throw std::runtime_error("worker deletion must precede BLE restore on idle writer core");
  host::workerStack=0;++host::deleted;host::parked=false;
}
int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,int stack,void* arg,int,TaskHandle_t* handle,int core){
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  if(host::currentTask!=writerHost::writerTask||host::bleReady||stack!=16384||core!=0||!handle||!tunnel::uploaderBusy()||tunnel::loggerIdle())
    throw std::runtime_error("upload stack allocated outside BLE-free writer-owned interval");
  if(!host::createTask)return 0;
  host::workerTask=fn;host::workerArg=arg;host::workerStack=stack;host::parked=false;*handle=reinterpret_cast<void*>(3);++host::created;
#else
  (void)stack;(void)handle;(void)core;if(!host::createTask)return 0;host::task=fn;host::taskArg=arg;
#endif
  return pdPASS;
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
  if(!uploaderBusy()||!loggerMaintenanceQuiescent()||loggerIdle()||host::workerStack)
    throw std::runtime_error("BLE released without exclusive idle writer or before stack release");
  ++host::pauses;host::bleReady=false;
  if(host::afterPause){auto fn=host::afterPause;host::afterPause=nullptr;fn();}
  return host::pauseOK;
}
bool scannersRestoreBleAfterUpload(){
  if(!uploaderBusy()||!loggerMaintenanceQuiescent()||loggerIdle()||host::workerStack||host::activeHttp||host::openFiles||!host::buffers.empty()||!host::disconnected)
    throw std::runtime_error("BLE restored before worker cleanup or after ownership release");
  ++host::restores;host::bleReady=host::restoreOK;
  if(host::afterRestore){auto callback=host::afterRestore;host::afterRestore=nullptr;callback();}
  return host::restoreOK;
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
  host::resetCheckedWrites();
  host::files.clear();host::directories={"/sessions"};host::failWritePath.clear();host::scripts.clear();host::calls.clear();host::connected=true;host::associations=0;host::afterWrite=nullptr;host::epoch=1;hostMillis=1000;
  if(!host::buffers.empty()||host::activeHttp||host::openFiles)throw std::runtime_error("Previous worker leaked ownership/resources");
  if(host::workerStack||writerHost::maintenanceState)throw std::runtime_error("Previous task/maintenance leaked");
  host::created=host::deleted=host::parkWaits=0;host::maintenanceOK=true;host::workerTask=nullptr;
  host::parked=host::deferPark=host::parkDeferred=false;
  host::afterPark=host::afterRestore=nullptr;host::currentTask=reinterpret_cast<void*>(1);
  writerHost::queue=1;writerHost::queued=0;writerHost::counters.log_drops=0;writerHost::openSession=writerHost::busy=writerHost::capturing=false;
  host::createTask=host::bleReady=host::pauseOK=host::restoreOK=host::disconnected=true;
  host::peakRegularFiles=host::bufferCalls=host::failBufferCall=host::checkpointCancels=0;host::fileHooks().afterFlush=nullptr;host::fileHooks().dynamicDirectory=false;
  host::pauses=host::restores=host::allocation=host::failAllocation=0;host::afterPause=nullptr;
  config=Config{};strlcpy(config.wifi_name,"Synthetic home",128);strlcpy(config.wifi_password,"testpass",128);strlcpy(config.wigle_name,"testname",128);strlcpy(config.wigle_token,"testtoken",128);strlcpy(config.wardrift_token,"testwardrift",128);strlcpy(config.wdgwars_token,"testwdgwars",128);
  config.auto_wigle=service==0;config.auto_wardrift=service==1;config.auto_wdgwars=service==2;
  busy=false;cancelled=false;requests=retryMask=jobPollMask=authBlocked=0;nextRetry=nextJobPoll=0;associationFailures=0;wasIdle=false;initialPending=true;settingsEpoch=1;
  uploaderBegin();host::files[sourcePath]=session();
}
void task(){
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  const auto before=host::pauses,restored=host::restores;
#endif
  if(host::task){auto fn=host::task;auto arg=host::taskArg;host::task=nullptr;try{fn(arg);}catch(const HostTaskFinished&){} }
  expect(!tunnel::uploaderBusy(),"worker releases busy state");
  expect(tunnel::loggerIdle()&&!tunnel::loggerMaintenanceQuiescent(),"idle only after maintenance completion; owner context does not escape");
  expect(host::workerStack==0&&host::created==host::deleted,"all upload task stacks reclaimed before ownership ends");
  expect(host::buffers.empty()&&host::activeHttp==0&&host::openFiles==0,"all worker buffers, HTTP and SD files released");
  expect(host::regularFiles==0&&host::peakRegularFiles<=2,"all uploader paths fit two regular SD file slots");
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  if(host::pauses>before&&host::pauseOK)expect(host::restores==restored+1,"every released BLE path restores before completion");
#else
  expect(host::pauses==0&&host::restores==0,"Heltec BLE lifetime is unchanged");
#endif
}
void automatic(){tunnel::uploaderTick(true,true);task();}
void restart(){using namespace tunnel;cancelled=false;requests=retryMask=jobPollMask=authBlocked=0;nextRetry=nextJobPoll=0;associationFailures=0;wasIdle=false;uploaderBegin();}
void manual(int service){expect(tunnel::uploaderRequest(service),"manual request accepted");tunnel::uploaderTick(true,false);task();}
bool metadataHas(const char* value){for(const auto& item:host::files)if(item.first.find(".json")!=std::string::npos&&item.second.find(value)!=std::string::npos)return true;return false;}

std::string checkpointState(int service,int chunk) {
  std::string result;uint32_t newest=0;bool found=false;
  const std::string key="/"+std::to_string(service)+"-"+std::to_string(chunk)+".";
  for(const auto& item:host::files)if(item.first.find(key)!=std::string::npos){
    StaticJsonDocument<1024> doc;if(deserializeJson(doc,item.second))continue;
    const uint32_t gen=doc["generation"]|0U;if(!found||int32_t(gen-newest)>0){found=true;newest=gen;result=doc["state"]|"";}}
  return result;
}
void replaceJobState(const char* state) {
  using namespace tunnel;
  for(const auto& item:host::files)if(item.first.find("/2-0.")!=std::string::npos){
    StaticJsonDocument<1024> doc;expect(!deserializeJson(doc,item.second),"job fixture checkpoint parses");
    const std::string directory=item.first.substr(0,item.first.rfind('/')),hash=doc["sha256"]|"";
    const size_t bytes=doc["source_bytes"];Checkpoint cp;expect(loadCheckpoint(directory.c_str(),2,0,hash.c_str(),bytes,cp),"load latest job fixture");
    expect(setState(directory.c_str(),2,0,hash.c_str(),bytes,cp,state),"persist newer job fixture state");return;
  }
  expect(false,"job fixture exists");
}
}
int main() {
  using namespace tunnel;
  storage::setOperationsForTest(&host::checkedOperations);
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

  // The real CheckedFile implementation receives failing C/VFS return values.
  // An immediately readable file is not enough: each pre-POST file must have
  // completed write, fflush, fsync and fclose successfully before transport.
  for(auto failure:{host::WriteFault::ShortWrite,host::WriteFault::FullWriteError,host::WriteFault::Flush,host::WriteFault::Sync,host::WriteFault::Close}) {
    for(const char* phase:{"staging","sending"}) {
      reset();const auto originalFile=host::files[sourcePath];host::writeFault=failure;host::faultState=phase;automatic();
      expect(host::writeFaults>0,"targeted staging/sending finalization fault was injected");
      expect(host::calls.empty()&&host::finalizedPosts==0,"failed staging or sending checkpoint prevents every POST");
      expect(host::files[sourcePath]==originalFile&&host::checkedHandles.empty()&&!host::busHeld,"write failure retains original and releases all handles/locks");
      expect(std::string(statusText).find("accepted; original log retained")==std::string::npos,"pre-send failure cannot claim accepted upload");
    }
    reset();host::writeFault=failure;host::faultState="accepted";automatic();
    expect(host::writeFaults>0&&host::calls.size()==1&&host::finalizedPosts==1,"accepted checkpoint fails only after finalized staging/sending and server receipt");
    expect(std::string(statusText)=="Receipt received; SD checkpoint failed","failed accepted persistence cannot claim completed success");
    expect(nextRetry==0&&retryMask==0&&nextJobPoll==0&&jobPollMask==0,"failed receipt checkpoint never schedules an implicit resend");
    bool committedAccepted=false;for(const auto& file:host::finalizedFiles)if(file.second.find("\"state\":\"accepted\"")!=std::string::npos)committedAccepted=true;
    expect(!committedAccepted,"failed receipt close cannot become a finalized accepted checkpoint");

    reset(2);host::writeFault=failure;host::faultState="job";host::scripts.push_back({202,"{\"job_id\":42}",7,false,false});automatic();
    expect(host::writeFaults>0&&host::calls.size()==1,"pending server job checkpoint fault injected after POST");
    expect(std::string(statusText)=="Job accepted; SD checkpoint failed"&&nextJobPoll==0&&jobPollMask==0,"failed job persistence never schedules polling or claims final acceptance");

    reset();host::writeFault=failure;host::faultState="uncertain";host::afterWrite=[](){uploaderCancel();};automatic();
    expect(host::writeFaults>0&&host::calls.size()==1,"cancelled POST result checkpoint failure injected");
    expect(nextRetry==0&&retryMask==0&&nextJobPoll==0&&jobPollMask==0&&!uploaderBusy()&&loggerIdle(),"failed cancellation checkpoint still finishes cleanup and suppresses rescheduling");
    expect(std::string(statusText)=="Upload cancelled; local logs retained","cancelled finalization failure never reports accepted upload");

    reset();Checkpoint cp;cp.state[0]='n';cp.boot=bootId;cp.attempts=2;cp.retryAt=12345;cp.epoch=4;
    expect(saveCheckpoint("/fixture",0,0,"hash",12,cp)&&cp.generation==1,"prepare committed checkpoint generation");
    const auto generation=cp.generation;strlcpy(cp.state,"accepted",sizeof(cp.state));host::writeFault=failure;host::faultState="accepted";
    expect(!saveCheckpoint("/fixture",0,0,"hash",12,cp)&&cp.generation==generation,"failed checkpoint finalization never advances in-memory generation");
  }

  for(const char* field:{"boot","retry_at","epoch"}) {
    reset();Checkpoint cp;cp.boot=bootId;cp.retryAt=12345;cp.epoch=4;
    static const char* altered=nullptr;altered=field;
    host::fileHooks().afterFlush=[](const std::string& path){
      StaticJsonDocument<1024> changed;if(deserializeJson(changed,host::files[path]))throw std::runtime_error("metadata readback fixture did not parse");
      changed[altered]=changed[altered].as<uint32_t>()+1;host::files[path].clear();serializeJson(changed,host::files[path]);host::fileHooks().afterFlush=nullptr;
    };
    expect(!saveCheckpoint("/fixture",0,0,"hash",12,cp)&&cp.generation==0,"readback rejects altered boot, retry deadline or credential epoch");
    expect(host::checkedHandles.empty()&&!host::busHeld,"metadata verification failure closes writer before readback");
  }

  reset();host::afterWrite=[](){tunnel::uploaderCancel();};automatic();expect(metadataHas("\"state\":\"uncertain\""),"cancellation after send preserves ambiguity");expect(nextRetry==0&&retryMask==0&&nextJobPoll==0&&jobPollMask==0,"cancel does not schedule itself again");

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
    expect(nextRetry==0&&retryMask==0&&nextJobPoll==0&&jobPollMask==0,"cancelled checkpoint cannot re-arm automatic retry or job polling");
    expect(metadataHas(service==2?"\"state\":\"job\"":"\"state\":\"retry\""),"cancellation retains the truthful persisted receipt state");
    const auto callsBefore=host::calls.size();hostMillis+=30001;uploaderTick(true,true);
    expect(host::task==nullptr&&!uploaderBusy()&&host::calls.size()==callsBefore,"no automatic worker or request thirty seconds after cancellation");
  }

  reset();host::files[sourcePath]=session(10000);automatic();expect(host::calls.size()>1,"large session split into bounded chunks");for(const auto& chunk:host::calls)expect(chunk.body.size()<=upload::CsvChunkLimit+400,"each multipart CSV chunk stays below 1 MiB plus envelope");const auto count=host::calls.size();restart();automatic();expect(host::calls.size()==count,"all accepted chunk checkpoints survive restart");
  reset();host::connected=false;automatic();expect(host::calls.empty(),"association failure sends no HTTP");
  reset();host::failAllocation=1;automatic();expect(host::calls.empty(),"line allocation failure sends no HTTP");
  reset();host::failAllocation=2;automatic();expect(host::calls.empty(),"row allocation failure sends no HTTP");
  reset();host::createTask=false;uploaderTick(true,true);
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  expect(uploaderBusy()&&!loggerIdle()&&host::pauses==0&&host::workerStack==0,"maintenance reserves idle without allocating upload stack");task();
  expect(host::pauses==1&&host::restores==1&&host::calls.empty(),"task creation failure restores BLE without network");
  reset();host::maintenanceOK=false;uploaderTick(true,true);expect(!uploaderBusy()&&!host::task&&host::pauses==0,"maintenance request failure preserves BLE and frees upload ownership");
#else
  expect(!uploaderBusy()&&!host::task&&host::calls.empty()&&host::pauses==0,"task creation failure never releases BLE or sends HTTP");
#endif
  reset();uploaderTick(true,true);uploaderCancel();task();expect(host::pauses==0&&host::calls.empty(),"cancel before worker entry never starts BLE transition or HTTP");
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  reset();host::deferPark=true;automatic();expect(host::parkWaits==1&&host::deleted==1&&host::restores==1,"published completion waits for suspension before reclaim and restore");
  reset();host::afterPark=[](){expect(uploaderBusy()&&!loggerIdle()&&host::workerStack==16384,"parked completion still owns task stack and gates capture");uploaderCancel();};automatic();
  expect(nextRetry==0&&nextJobPoll==0&&host::deleted==1&&host::restores==1,"cancel after worker cleanup still deletes stack and restores BLE");
  reset();host::afterRestore=[](){expect(uploaderBusy()&&!loggerIdle()&&host::workerStack==0,"restore retains uploader/maintenance ownership with upload stack absent");uploaderCancel();};automatic();
  expect(nextRetry==0&&nextJobPoll==0&&!uploaderBusy()&&loggerIdle(),"cancel during restore cannot rearm work or retain busy");
  reset();uploaderTick(true,true);
  expect(uploaderBusy()&&!loggerIdle()&&!loggerMaintenanceQuiescent()&&host::workerStack==0,"pending maintenance blocks main capture/config without granting owner context");
  expect(!writerHost::loggerRequestMaintenance([](void*){},nullptr,[](){}),"second pending maintenance rejected");
  for(auto kind:{writerHost::Kind::Wifi,writerHost::Kind::Ble,writerHost::Kind::LoRa,writerHost::Kind::Start,writerHost::Kind::Stop})
    expect(!writerHost::logObservation({kind}),"maintenance rejects all new observations and markers");
  expect(writerHost::counters.log_drops==3&&writerHost::queued==0,"refused real observations count drops; retryable Start/Stop do not");task();
  for(unsigned reason=0;reason<5;++reason) {
    reset();writerHost::capturing=reason==0;writerHost::busy=reason==1;writerHost::openSession=reason==2;writerHost::queued=reason==3?1:0;if(reason==4)writerHost::queue=0;
    expect(!writerHost::loggerRequestMaintenance([](void*){},nullptr,[](){}),"maintenance rejects capture, writer busy, open file, queued work or absent writer");
    expect(writerHost::maintenanceState==0&&!loggerMaintenanceQuiescent(),"refused maintenance never claims ownership");
  }
  reset();expect(!writerHost::loggerRequestMaintenance(nullptr,nullptr,[](){})&&!writerHost::loggerRequestMaintenance([](void*){},nullptr,nullptr),"invalid maintenance callbacks rejected");
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

  // Empty/no-new work must not retain the association message or imply receipt.
  reset();host::files[sourcePath]=session(0);automatic();
  expect(checkpointState(0,-2)=="skipped"&&!metadataHas("\"state\":\"accepted\""),"empty whole-file summary is skipped, never server accepted");
  expect(std::string(statusText)=="No eligible data; nothing uploaded","empty session has a truthful completion status");
  restart();automatic();expect(std::string(statusText)=="No new data; already processed","already skipped session reports no new work");
  reset();host::files.clear();automatic();expect(host::calls.empty()&&std::string(statusText)=="No eligible data; nothing uploaded","empty session directory has a final non-connecting status");
  reset();automatic();restart();automatic();expect(std::string(statusText)=="No new data; already processed","already accepted data does not fabricate a new receipt");
  // Older firmware marked empty summaries accepted. Retain checkpoint compatibility
  // without exposing that local completion state as a newly accepted network receipt.
  reset();host::files[sourcePath]=session(0);automatic();
  for(auto& item:host::files)if(item.first.find("/0--2.")!=std::string::npos){
    const auto at=item.second.find("skipped");if(at!=std::string::npos)item.second.replace(at,7,"accepted");}
  restart();automatic();expect(host::calls.empty()&&std::string(statusText).find("accepted")==std::string::npos,"legacy empty summary is not a service acceptance claim");

  // Explicit manual WDG request may continue GET polling on the Uploads page
  // with all automatic services off, but cannot stage or submit new sessions.
  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);
  expect(nextJobPoll!=0&&jobPollMask==4&&nextRetry==0&&retryMask==0,"pending job uses bounded separate poll schedule");
  host::files[staging]="STAGING_SENTINEL";host::files["/sessions/LT-87654321.jsonl"]=session(2);
  hostMillis+=30001;uploaderTick(false,false,true);expect(!host::task&&jobPollMask==4,"active capture blocks and retains pending poll");
  uploaderTick(true,false,false);expect(!host::task&&jobPollMask==4,"editing and Power guard block and retain pending poll");
  host::scripts.push_back({200,"{\"status\":\"done\"}",3,false,false});uploaderTick(true,false,true);task();
  expect(host::calls.size()==2&&host::calls.back().config.method==HTTP_METHOD_GET&&host::calls.back().body.empty(),"status-page continuation is GET only despite another unuploaded session");
  expect(host::files[staging]=="STAGING_SENTINEL","poll-only worker never rewrites CSV staging");
  expect(checkpointState(2,0)=="accepted"&&nextJobPoll==0&&jobPollMask==0,"completed poll persists accepted receipt and stops polling");

  host::files.erase("/sessions/LT-87654321.jsonl");manual(2);
  expect(host::calls.size()==2&&std::string(statusText)=="No new data; already processed","manually revisiting completed polled chunks reports prior completion without a new receipt");

  // Merging a due poll with a different enabled service must not permit a new
  // WDG POST when WDG automatic is disabled.
  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);
  config.auto_wigle=true;host::files["/sessions/LT-87654321.jsonl"]=session(2);hostMillis+=30001;
  host::scripts.push_back({200,"{\"success\":true}",3,false,false});
  host::scripts.push_back({200,"{\"status\":\"done\"}",3,false,false});
  host::scripts.push_back({200,"{\"success\":true}",3,false,false});automatic();
  unsigned wdgPosts=0;for(const auto& call:host::calls)if(call.url.find("wdgwars.pl")!=std::string::npos&&call.config.method==HTTP_METHOD_POST)++wdgPosts;
  expect(wdgPosts==1,"other automatic service cannot turn WDG continuation into unsolicited POST");

  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);
  config.auto_wigle=true;hostMillis+=30001;host::connected=false;automatic();
  expect((jobPollMask.load()&4)&&!(retryMask.load()&4),"mixed-service association failure retains GET-only WDG retry classification");
  host::connected=true;host::files["/sessions/LT-87654321.jsonl"]=session(2);hostMillis+=30001;
  host::scripts.push_back({200,"{\"success\":true}",3,false,false});host::scripts.push_back({200,"{\"status\":\"done\"}",3,false,false});host::scripts.push_back({200,"{\"success\":true}",3,false,false});automatic();
  wdgPosts=0;for(const auto& call:host::calls)if(call.url.find("wdgwars.pl")!=std::string::npos&&call.config.method==HTTP_METHOD_POST)++wdgPosts;
  expect(wdgPosts==1,"association retry cannot convert poll permission into WDG POST permission");

  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);
  for(auto it=host::files.begin();it!=host::files.end();)if(it->first.find("/2-0.b.json")!=std::string::npos)it=host::files.erase(it);else ++it;
  host::fileHooks().dynamicDirectory=true;hostMillis+=30001;
  host::scripts.push_back({200,"{\"status\":\"processing\"}",3,false,false});uploaderTick(true,false,true);task();
  expect(host::calls.size()==2&&checkpointState(2,0)=="job","mutable FAT iterator cannot immediately repoll a newly created alternate slot");
  expect(nextJobPoll>hostMillis&&jobPollMask==4,"persisted per-job due preserves thirty-second poll cadence");
  hostMillis+=30001;host::scripts.push_back({200,"{\"status\":\"done\"}",3,false,false});uploaderTick(true,false,true);task();
  expect(host::calls.size()==3&&checkpointState(2,0)=="accepted","next due poll completes normally after mutable-directory guard");

  for(const char* terminal:{"accepted","rejected"}) {
    reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);replaceJobState(terminal);
    host::files[staging]="STAGING_SENTINEL";hostMillis+=30001;uploaderTick(true,false,true);task();
    expect(host::calls.size()==1&&host::files[staging]=="STAGING_SENTINEL","newest terminal checkpoint supersedes older pending-job slot");
    expect(std::string(statusText)=="No pending upload jobs","stale pending mask has truthful completion status");
  }
  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);
  replaceJobState("authjob");authBlocked=4;hostMillis+=30001;uploaderTick(true,false,true);
  expect(!host::task&&host::calls.size()==1,"auth blocked service cannot poll from status page");
  host::scripts.push_back({200,"{\"status\":\"done\"}",3,false,false});manual(2);
  expect(host::calls.size()==2&&host::calls.back().config.method==HTTP_METHOD_GET,"manual auth retry polls retained job without reupload");

  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);
  uploaderCancel();hostMillis+=30001;uploaderTick(true,false,true);expect(!host::task&&nextJobPoll==0&&jobPollMask==0,"cancel clears pending GET schedule on status page");
  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);
  hostMillis+=30001;uploaderTick(true,false,true);uploaderCancel();task();
  expect(host::calls.size()==1&&nextJobPoll==0&&jobPollMask==0,"cancel after poll worker scheduled prevents GET and retry rearm");
  // Automatic off plus a reboot requires an explicit Upload now to rediscover
  // old jobs; entering the status page alone never initiates network work.
  reset(2);config.auto_wdgwars=false;host::scripts.push_back({202,"{\"job_id\":42}",3,false,false});manual(2);restart();hostMillis+=30001;
  uploaderTick(true,false,true);expect(!host::task&&host::calls.size()==1,"automatic-off persisted job is dormant after reboot until manual request");
  host::scripts.push_back({200,"{\"status\":\"done\"}",3,false,false});manual(2);
  expect(host::calls.size()==2&&host::calls.back().config.method==HTTP_METHOD_GET,"manual request after reboot rediscovers persisted job");
  unsigned parsedChunk=0,parsedSlot=0;
  expect(checkpointChunk("2-2147483647.b.json",parsedChunk,parsedSlot)&&parsedChunk==2147483647U&&parsedSlot==1,"checkpoint filename parser is bounded");
  for(const char* name:{"2-2147483648.a.json","2--1.a.json","2-00.a.json","1-0.a.json","2-0.a.json/extra","2-0.a.jsonx"})expect(!checkpointChunk(name,parsedChunk,parsedSlot),"noncanonical checkpoint filename rejected");

  std::cout<<"Uploader host: "<<checks<<" checks passed against real worker with mocked SD/TLS transport. No network requests made.\n";
}
