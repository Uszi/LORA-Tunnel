#include <Arduino.h>
#include <ArduinoJson.h>
#include <iostream>
#include <map>
namespace host {
bool failScratch=false,failRules=false;size_t scratchLive=0,scratchPeak=0,scratchCalls=0,heapScratchCalls=0;
struct ScratchAllocator {
  void* allocate(size_t bytes){++heapScratchCalls;++scratchCalls;if(failScratch)return nullptr;void* p=std::malloc(bytes);if(p){scratchLive+=bytes;scratchPeak=std::max(scratchPeak,scratchLive);}return p;}
  void deallocate(void* pointer){if(pointer)scratchLive-=16384;std::free(pointer);}
  void* reallocate(void*,size_t){return nullptr;}
};
}
void* operator new[](std::size_t bytes,const std::nothrow_t&) noexcept {return host::failRules?nullptr:std::malloc(bytes);}
void operator delete[](void* pointer) noexcept {std::free(pointer);}
void operator delete[](void* pointer,std::size_t) noexcept {std::free(pointer);}
using HostScopedJsonDocument=ArduinoJson::BasicJsonDocument<host::ScratchAllocator>;
#define DynamicJsonDocument HostScopedJsonDocument
#include "../../src/config.cpp"
#undef DynamicJsonDocument
namespace host {
uint32_t epoch=1;
unsigned openFiles=0,regularFiles=0,peakRegularFiles=0,bufferCalls=0,failBufferCall=0;
std::map<std::string,std::string> files,preferences;std::vector<std::string> directories;
std::string failWritePath,failOpenPath;bool failRename=false,failPreferences=false;
}
ConfigSD SD;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
#include "checked_backend.h"
#endif
namespace tunnel {namespace recon {
bool parseAddress(const char* text,uint8_t result[6]) {unsigned bytes[6];if(!text||std::sscanf(text,"%02x:%02x:%02x:%02x:%02x:%02x",bytes,bytes+1,bytes+2,bytes+3,bytes+4,bytes+5)!=6)return false;for(unsigned i=0;i<6;++i)result[i]=uint8_t(bytes[i]);return true;}
}}
namespace {
unsigned checks=0;
void expect(bool value,const char* description){++checks;if(!value){std::cerr<<"FAIL "<<description<<"\n";std::exit(1);}}
const char* primary="/lora-tunnel/config.json";
const char* ruleFile="/lora-tunnel/detection_rules.json";
const char* initial=R"({"schema":1,"lora":{"region":"UNSET","selection_complete":false,"enabled":false},"credentials":{"wigle_api_name":"old-name","wigle_api_token":"old-token","wifi_ssid":"Home","wifi_password":"password"},"extension":{"preserved":"yes"}})";
void reset(){
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  host::resetCheckedFaults();
#endif
expect(host::openFiles==0,"all file handles closed between operations");host::bufferCalls=host::failBufferCall=host::peakRegularFiles=0;host::files.clear();host::directories.clear();host::preferences.clear();host::failWritePath.clear();host::failOpenPath.clear();host::failRename=host::failPreferences=host::failScratch=false;host::scratchCalls=host::scratchPeak=host::heapScratchCalls=0;expect(host::scratchLive==0,"no document survives a prior operation");host::files[primary]=initial;host::files[ruleFile]=R"({"schema":1,"rules":[]})";}
void released(){expect(host::scratchLive==0,"configuration scratch released on return");
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
 expect(!host::leaseActive&&host::checkedHandles.empty(),"lease and all checked file handles end on return");
 expect(std::all_of(host::frameScratch,host::frameScratch+16896,[](uint8_t b){return b==0;}),"all JSON and file-buffer secret bytes are wiped on release");
#endif
}
}
int main(){using namespace tunnel;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  storage::setOperationsForTest(&host::checkedOperations);
#endif
  reset();expect(loadConfig(),"load valid settings");released();expect(rules==nullptr&&ruleCount==0,"empty rules allocate no persistent slab");expect(!configAllocationFailed&&!rulesLoadFailed,"successful load has clear failure flags");
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  expect(host::scratchPeak==16896,"16 KiB JSON and512-byte I/O share the existing framebuffer lease");
  {ScopedConfigDocument operation;expect(operation.document.capacity()==16384,"full accepted JSON arena capacity is retained");}
  released();
#else
  expect(host::scratchPeak==16384,"Heltec retains full heap document capacity");
#endif
  expect(saveCredential(1,"replacement-token"),"save credential");released();expect(!std::strcmp(config.wigle_token,"replacement-token"),"runtime credential updated after verified save");expect(host::files[primary].find("\"preserved\":\"yes\"")!=std::string::npos,"unknown document fields preserved");expect(host::files.count("/lora-tunnel/config.bak")>0,"atomic backup retained");
  expect(saveAutoUpload(1,true),"save service toggle");released();expect(config.auto_wardrift,"runtime service toggle applied");
  const auto region=saveLoRaRegion("US",true);released();expect(region.applied&&region.sd_saved&&region.nvs_saved,"SD-first region persistence unchanged");expect(host::preferences["lt-region:code"]=="US","NVS code readback stored");
  host::failScratch=true;const auto saved=host::files[primary];const std::string token=config.wigle_token;
  expect(!saveCredential(1,"other"),"credential allocation failure reported");released();expect(configAllocationFailed&&host::files[primary]==saved&&config.wigle_token==token,"failed allocation changes neither SD nor active credential");
  expect(!saveAutoUpload(0,true),"toggle allocation failure reported");released();expect(!config.auto_wigle,"failed toggle preserves runtime");
  const auto failed=saveLoRaRegion("EU_868",true);released();expect(!failed.applied&&!failed.sd_saved&&!failed.nvs_saved,"SD allocation failure does not fall through into NVS region change");expect(host::preferences["lt-region:code"]=="US","failed SD save preserves NVS");
  const size_t before=host::scratchCalls;const auto nvs=saveLoRaRegion("EU_868",false);expect(nvs.applied&&!nvs.sd_saved&&nvs.nvs_saved,"NVS-only region works without scratch");expect(host::scratchCalls==before,"NVS-only region allocates no JSON arena");
  expect(!loadConfig(),"load allocation failure reported");released();expect(configAllocationFailed&&!config.lora_enabled&&configLoadState==ConfigLoadState::Invalid,"failed startup remains LoRa off");
  host::failScratch=false;expect(loadConfig(),"load recovers after allocation availability");released();
  host::failWritePath="config.tmp";const auto beforeWrite=host::files[primary];expect(!saveCredential(1,"write-fails"),"temporary write failure reported");released();expect(host::files[primary]==beforeWrite&&!host::files.count("/lora-tunnel/config.tmp"),"failed temp write preserves primary and cleans temporary file");host::failWritePath.clear();
  std::string ruleText="{\"schema\":1,\"rules\":[";for(unsigned i=0;i<65;++i){if(i)ruleText+=',';ruleText+="{\"type\":\"name\",\"match\":\"Example\",\"label\":\"Demo\"}";}ruleText+="]}";host::files[ruleFile]=ruleText;
  expect(loadConfig(),"load a populated custom rule document");released();expect(rules!=nullptr&&ruleCount==64,"64-rule maximum preserved");Rule* previous=rules;
  host::failRules=true;expect(!loadConfig()&&configAllocationFailed&&rulesLoadFailed,"rule-slab allocation failure reported");released();expect(rules==previous&&ruleCount==64,"failed rule allocation preserves complete prior set");host::failRules=false;
  host::failOpenPath=ruleFile;expect(!loadConfig()&&rulesLoadFailed,"existing but unopenable rules report failure");released();expect(rules==previous&&ruleCount==64,"existing rule open failure preserves complete prior set");host::failOpenPath.clear();
  host::files[ruleFile]="{broken";expect(!loadConfig()&&rulesLoadFailed,"invalid rules are visibly rejected");released();expect(rules==previous&&ruleCount==64,"invalid replacement preserves prior complete rule set");
  host::files.erase(ruleFile);expect(loadConfig(),"missing custom rules accepted as empty");released();expect(rules==nullptr&&ruleCount==0,"missing rules release old slab");
  reset();expect(loadConfig(),"baseline for existing configuration open failure");released();
  const auto sourceBefore=host::files[primary];const std::string activeBefore=config.wigle_token;
  host::preferences["lt-region:code"]="EU_868";host::failOpenPath=primary;
  expect(!saveCredential(1,"must-not-replace"),"unopenable existing primary rejects credential save");released();
  expect(host::files[primary]==sourceBefore && config.wigle_token==activeBefore && !host::files.count("/lora-tunnel/config.tmp"),"unopenable source cannot be seeded or replaced");
  expect(!saveAutoUpload(0,true) && !config.auto_wigle,"unopenable source cannot change service preference");released();
  const auto unreadableRegion=saveLoRaRegion("US",true);
  expect(!unreadableRegion.applied && !unreadableRegion.sd_saved && !unreadableRegion.nvs_saved && host::preferences["lt-region:code"]=="EU_868","unopenable source cannot fall through into NVS region change");released();
  host::files["/lora-tunnel/config.bak"]=sourceBefore;
  expect(!loadConfig() && configLoadState==ConfigLoadState::Invalid && !config.lora_enabled,"existing source open failure does not fall back to backup or NVS radio settings");released();
  host::failOpenPath.clear();host::files[primary]="{bad";host::failOpenPath="/lora-tunnel/config.bak";
  expect(!saveCredential(1,"must-not-replace") && host::files[primary]=="{bad" && host::files["/lora-tunnel/config.bak"]==sourceBefore,"unopenable existing backup is preserved after invalid primary");released();
  host::failOpenPath.clear();
  reset();host::files["/lora-tunnel/config.bak"]=host::files[primary];host::files[primary]="{bad";expect(loadConfig()&&configLoadState==ConfigLoadState::Backup,"backup recovery preserved");released();
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  reset();expect(loadConfig(),"Cardputer baseline for file buffer faults");released();
  const std::string original=host::files[primary],activeToken=config.wigle_token;
  for(unsigned fault=1;fault<=3;++fault) {
    host::bufferCalls=0;host::failBufferCall=fault;
    expect(!saveCredential(1,"must-not-save"),"source/temp/readback file buffer failure rejects credential save");released();
    expect(configAllocationFailed && host::files[primary]==original && config.wigle_token==activeToken,"failed buffer preserves original document and live token");
    expect(!host::files.count("/lora-tunnel/config.tmp") && host::regularFiles==0,"failed buffer closes handle and removes temporary output");
  }
  host::bufferCalls=0;host::failBufferCall=1;
  const auto rejectedRegion=saveLoRaRegion("US",true);
  expect(!rejectedRegion.applied && !rejectedRegion.sd_saved && !rejectedRegion.nvs_saved,"file buffer failure cannot commit NVS region");released();
  host::bufferCalls=0;host::failBufferCall=1;
  expect(!loadConfig() && configAllocationFailed && configLoadState==ConfigLoadState::Invalid && !config.lora_enabled,"startup file buffer failure remains visibly invalid and LoRa off");released();
  host::bufferCalls=0;host::failBufferCall=2;
  expect(!loadConfig() && configAllocationFailed && rulesLoadFailed && host::regularFiles==0,"rule file buffer failure closes file and reports rule failure");released();
  host::failBufferCall=0;expect(loadConfig(),"load recovers after file buffer failure");released();
  expect(host::peakRegularFiles<=1,"configuration uses at most one regular file handle");
#endif

  // A large accepted extension must survive every setting operation, including
  // credentials at their maximum supported length and nested unknown values.
  reset();
  const std::string large(12000,'Z');
  host::files[primary]=std::string(initial).substr(0,std::strlen(initial)-1)+",\"large\":\""+large+"\",\"extra\":[null,true,17,{\"keep\":\"quoted \\\" and slash \\\\\"}]}";
  expect(loadConfig(),"large unknown fields remain accepted at full arena capacity");released();
  const std::string maxToken(127,'T');
  expect(saveCredential(1,maxToken.c_str()),"maximum credential saves beside large unknown fields");released();
  expect(saveAutoUpload(2,true),"large document service preference saves");released();
  expect(saveLoRaRegion("US",true).sd_saved,"large document region saves");released();
  ArduinoJson::StaticJsonDocument<16384> parsed;
  expect(!deserializeJson(parsed,host::files[primary]),"large saved document parses intact");
  expect(parsed["large"].as<std::string>()==large&&parsed["credentials"]["wigle_api_token"].as<std::string>()==maxToken&&parsed["extension"]["preserved"]=="yes","large and legacy unknown fields plus credential preserved");
  expect(parsed["extra"][0].isNull()&&parsed["extra"][1]==true&&parsed["extra"][2]==17&&parsed["extra"][3]["keep"].is<const char*>(),"nested unknown values and escapes survive verified save");
  // File bytes can fit while a dense JSON array needs more than16 KiB of slots.
  // NoMemory is not permission to seed over that original or change NVS.
  reset();std::string dense=std::string(initial).substr(0,std::strlen(initial)-1)+",\"dense\":[";
  for(unsigned i=0;i<1200;++i){if(i)dense+=',';dense+="{}";}dense+="]}";
  expect(dense.size()<16384,"adversarial dense input is within the unchanged file limit");host::files[primary]=dense;
  host::preferences["lt-region:code"]="EU_868";
  const auto denseResult=saveLoRaRegion("US",true);released();
  expect(!denseResult.applied&&configAllocationFailed&&host::files[primary]==dense&&host::preferences["lt-region:code"]=="EU_868","arena exhaustion preserves original and NVS without fallback seeding");
  expect(!loadConfig()&&configAllocationFailed&&configLoadState==ConfigLoadState::Invalid,"dense import fails visibly without heap fallback");released();
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  struct Fault {std::string* path;const char* file;const char* name;};
  const Fault faults[]={
    {&host::failReadPath,primary,"source read"},
    {&host::failClosePath,primary,"source close"},
    {&host::failStatPath,primary,"source stat"},
    {&host::failFlushPath,"/lora-tunnel/config.tmp","temporary flush"},
    {&host::failSyncPath,"/lora-tunnel/config.tmp","temporary fsync"},
    {&host::failClosePath,"/lora-tunnel/config.tmp","temporary close"},
    {&host::failReadPath,"/lora-tunnel/config.tmp","readback read"},
    {&host::failStatPath,"/lora-tunnel/config.bak","backup stat"}
  };
  for(const auto& fault:faults){
    reset();expect(loadConfig(),"fault baseline loads");released();
    const auto preserved=host::files[primary];const std::string oldToken=config.wigle_token;
    host::preferences["lt-region:code"]="EU_868";*fault.path=fault.file;
    expect(!saveCredential(1,"must-not-commit"),fault.name);released();
    expect(host::files[primary]==preserved&&config.wigle_token==oldToken&&!host::files.count("/lora-tunnel/config.tmp"),"I/O failure preserves primary/live credential and removes temp");
    expect(!saveLoRaRegion("US",true).applied&&host::preferences["lt-region:code"]=="EU_868","I/O failure cannot fall through into NVS");released();
    fault.path->clear();expect(saveCredential(1,"recovered"),"next save recovers after I/O fault");released();
    expect(host::heapScratchCalls==0&&host::scratchPeak==16896,"successful and failed saves never allocate a JSON heap arena");
  }
  reset();expect(loadConfig(),"mkdir fault baseline loads");released();const auto untouched=host::files[primary];host::failMkdir=true;
  expect(!saveCredential(1,"reject")&&host::files[primary]==untouched,"directory failure rejects save before any source mutation");released();
  host::failMkdir=false;expect(saveCredential(1,"after-mkdir"),"save recovers after directory failure");released();
  // Even an explicitly denied lease leaves the UI buffer and on-disk file intact.
  const auto safe=host::files[primary];host::failScratch=true;const auto heldCalls=host::bufferCalls;
  expect(!saveAutoUpload(0,true)&&host::files[primary]==safe&&host::bufferCalls==heldCalls,"unavailable framebuffer lease performs no SD I/O or heap fallback");released();host::failScratch=false;
#endif
  std::cout<<"Config memory host: "<<checks<<" checks passed; full16 KiB arena, scoped lease/heap and exact rule storage verified.\n";
}
