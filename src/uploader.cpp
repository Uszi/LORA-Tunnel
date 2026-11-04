#include "uploader.h"
#include "scanners.h"
#include "upload_csv.h"
#include "upload_protocol.h"
#include "credential_validation.h"
#include "config.h"
#include "app_types.h"
#include "logger.h"
#include "checked_sd_file.h"
#include "board_config.h"
#include "version.h"
#include <SD.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <atomic>
#include <new>
#include <ctime>
#include <algorithm>

namespace tunnel {
namespace {
constexpr char root[]="/lora-tunnel/uploads";
constexpr char staging[]="/lora-tunnel/uploads/staging.csv";
constexpr const char* names[]={"WiGLE","Wardrift","WDGWars"};
constexpr const char* endpoints[]={"https://api.wigle.net/api/v2/file/upload","https://wardrift.net/v1/ingest/signals","https://wdgwars.pl/api/v2/upload-csv"};
constexpr size_t LineCapacity=8193;
std::atomic<bool> busy{false},cancelled{false};
std::atomic<unsigned> requests{0},retryMask{0},authBlocked{0};
std::atomic<uint32_t> nextRetry{0},nextJobPoll{0};
std::atomic<unsigned> jobPollMask{0};
bool wasIdle=false,initialPending=true;
uint32_t bootId=0,settingsEpoch=1;
std::atomic<unsigned> associationFailures{0};
portMUX_TYPE statusMux=portMUX_INITIALIZER_UNLOCKED;
char statusText[64]="Uploads off; configure keys and services";
struct Work { Config settings; unsigned mask=0,manual=0,pollMask=0; uint32_t epoch=1;
  bool pollOnly=false,resultReported=false,alreadyProcessed=false,skippedFiles=false,selfCheck=false; };
struct StorageLock {StorageLock(){xSemaphoreTake(busMutex,portMAX_DELAY);}~StorageLock(){xSemaphoreGive(busMutex);}};
// Each writer owns one bounded stdio buffer, with no hidden allocation. Explicit
// checked close decides success; RAII only guarantees locked cleanup on errors.
struct UploadWriter {
  uint8_t buffer[512];
  storage::CheckedFile file;
  size_t written=0;
  ~UploadWriter(){StorageLock lock;file.close();}
  explicit operator bool() const {return bool(file);}
  size_t size() const {return written;}
};
void closeFile(UploadWriter& output){StorageLock lock;output.file.close();}
void status(const char* text){portENTER_CRITICAL(&statusMux);strlcpy(statusText,text,sizeof(statusText));portEXIT_CRITICAL(&statusMux);}
void serviceStatus(int service,const char* text){char line[64];snprintf(line,sizeof(line),"%s: %s",names[service],text);status(line);}
void workStatus(Work& work,const char* text){work.resultReported=true;status(text);}
void workServiceStatus(Work& work,int service,const char* text){work.resultReported=true;serviceStatus(service,text);}
std::atomic<uint32_t> uploadRuns{0},uploadMinFree{0},uploadSmallestBlock{0},uploadStackMargin{0},selfCheckState{0};
void sampleUploadResources() {
  const uint32_t available=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  const uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  const uint32_t margin=uxTaskGetStackHighWaterMark(nullptr);
  if(!uploadMinFree.load() || available<uploadMinFree.load())uploadMinFree=available;
  if(!uploadSmallestBlock.load() || largest<uploadSmallestBlock.load())uploadSmallestBlock=largest;
  if(!uploadStackMargin.load() || margin<uploadStackMargin.load())uploadStackMargin=margin;
}
// Ephemeral parser memory is completely absent during HTTP and job polling.
// The allocator is explicit so tests account for the JSON arena as well as the
// line/row buffers. Existing evidence and parser size limits are unchanged.
struct UploadAllocator {
  void* allocate(size_t bytes){return malloc(bytes);}
  void deallocate(void* memory){free(memory);}
};
using UploadDocument=BasicJsonDocument<UploadAllocator>;
struct ParserStorage {
  char* line=nullptr;char* row=nullptr;UploadDocument* doc=nullptr;
  ParserStorage()=default;
  ParserStorage(const ParserStorage&)=delete;
  ParserStorage& operator=(const ParserStorage&)=delete;
  ~ParserStorage(){release();}
  void release(){delete doc;doc=nullptr;free(line);line=nullptr;free(row);row=nullptr;}
  bool acquire(bool withRow) {
    if(!line)line=static_cast<char*>(malloc(LineCapacity));
    if(!doc)doc=new(std::nothrow) UploadDocument(4096);
    if(withRow&&!row)row=static_cast<char*>(malloc(upload::CsvRowCapacity));
    const bool ok=line&&doc&&doc->capacity()>=4096&&(!withRow||row);
    sampleUploadResources();if(!ok)release();return ok;
  }
};
bool stopped(){return cancelled.load(std::memory_order_relaxed);}
bool delayUntil(uint32_t duration){const uint32_t start=millis();while(millis()-start<duration){if(stopped())return false;vTaskDelay(pdMS_TO_TICKS(50));}return true;}
// Keep Cardputer's stdio cache bounded; every caller already owns StorageLock.
File openFile(const char* path,const char* mode=FILE_READ) {
  File file=SD.open(path,mode);
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  if(file&&!file.isDirectory()&&!file.setBufferSize(512))file.close();
#endif
  return file;
}
void closeFile(File& file){StorageLock lock;if(file)file.close();}
bool fileSize(const char* path,size_t& size){StorageLock lock;File file=openFile(path,FILE_READ);if(!file)return false;size=file.size();file.close();return true;}
unsigned enabled(const Config& settings){return (settings.auto_wigle?1U:0U)|(settings.auto_wardrift?2U:0U)|(settings.auto_wdgwars?4U:0U);}
bool credentials(const Config& c,int service) {
  if(!c.wifi_name[0]||!validCredential(3,c.wifi_name)||!validCredential(4,c.wifi_password))return false;
  if(service==0)return c.wigle_name[0]&&c.wigle_token[0]&&validCredential(0,c.wigle_name)&&validCredential(1,c.wigle_token);
  if(service==1)return c.wardrift_token[0]&&validCredential(5,c.wardrift_token);
  return service==2&&c.wdgwars_token[0]&&validCredential(2,c.wdgwars_token);
}
void scheduleRetry(int service,uint32_t delay) {
  retryMask.fetch_or(1U<<service);const uint32_t at=millis()+delay,old=nextRetry.load();
  if(!old || int32_t(at-old)<0)nextRetry=at;
}
void scheduleJobPoll(int service,uint32_t delay) {
  jobPollMask.fetch_or(1U<<service);const uint32_t at=millis()+delay,old=nextJobPoll.load();
  if(!old || int32_t(at-old)<0)nextJobPoll=at;
}
// One line is bounded by the native writer's 8192-byte serialized-record limit.
// Oversized/imported lines are drained and skipped without unbounded allocation.
int readLine(File& file,char* line,size_t capacity) {
  StorageLock lock;size_t used=0;bool overflow=false,any=false;
  while(file.available()) {
    const int c=file.read();if(c<0)return -2;any=true;
    if(used+1<capacity)line[used++]=char(c);else overflow=true;
    if(c=='\n')break;
    if((used&255)==0 && stopped())return -2;
  }
  line[used]=0;if(!any)return 0;return overflow?-1:int(used);
}
bool closedSession(const char* path,char digest[65],size_t& bytes,char* buffer,UploadDocument& doc) {
  File file;{StorageLock lock;file=openFile(path,FILE_READ);if(!file)return false;bytes=file.size();}
  if(bytes<2){closeFile(file);return false;}
  const int first=readLine(file,buffer,LineCapacity);
  bool ok=first>0&&!deserializeJson(doc,buffer)&&upload::integer(doc["schema"],1,1)&&
      !strcmp(doc["type"]|"","session_start")&&upload::wardriverMode(doc.as<JsonVariantConst>())&&!upload::synthetic(doc.as<JsonVariantConst>());
  char session[16]{};if(ok)strlcpy(session,doc["session"]|"",sizeof(session));
  if(!ok||strlen(session)!=8){closeFile(file);return false;}
  // Read only the bounded final record to establish a completed matching session.
  size_t tail=std::min(bytes,LineCapacity-1);
  {StorageLock lock;ok=file.seek(bytes-tail)&&file.read(reinterpret_cast<uint8_t*>(buffer),tail)==tail;}
  if(!ok||buffer[tail-1]!='\n'){closeFile(file);return false;}
  buffer[--tail]=0;while(tail && (buffer[tail-1]=='\r'||buffer[tail-1]=='\n'))buffer[--tail]=0;
  char* last=buffer+tail;while(last>buffer&&last[-1]!='\n')--last;
  ok=!deserializeJson(doc,last)&&upload::integer(doc["schema"],1,1)&&!strcmp(doc["type"]|"","session_end")&&
      !strcmp(doc["session"]|"",session)&&upload::wardriverMode(doc.as<JsonVariantConst>())&&!upload::synthetic(doc.as<JsonVariantConst>());
  if(!ok){closeFile(file);return false;}
  mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);mbedtls_sha256_starts(&hash,0);
  {StorageLock lock;ok=file.seek(0);}
  size_t consumed=0;
  while(ok&&!stopped()&&consumed<bytes) {
    const size_t amount=std::min(size_t(1024),bytes-consumed);size_t got;
    {StorageLock lock;got=file.read(reinterpret_cast<uint8_t*>(buffer),amount);}
    if(got!=amount){ok=false;break;}
    mbedtls_sha256_update(&hash,reinterpret_cast<uint8_t*>(buffer),got);consumed+=got;taskYIELD();
  }
  uint8_t raw[32];mbedtls_sha256_finish(&hash,raw);mbedtls_sha256_free(&hash);closeFile(file);
  if(!ok||stopped()||consumed!=bytes)return false;
  constexpr char digits[]="0123456789abcdef";for(unsigned i=0;i<32;++i){digest[i*2]=digits[raw[i]>>4];digest[i*2+1]=digits[raw[i]&15];}digest[64]=0;return true;
}
struct Checkpoint {
  uint32_t generation=0,boot=0,attempts=0,retryAt=0,epoch=0;
  char state[12]="new",job[81]{};
};
void checkpointPath(char path[144],const char* directory,int service,int chunk,unsigned slot) {
  snprintf(path,144,"%s/%d-%d.%c.json",directory,service,chunk,slot?'b':'a');
}
bool loadCheckpoint(const char* directory,int service,int chunk,const char* hash,size_t bytes,Checkpoint& result) {
  result=Checkpoint{};bool found=false,existed=false;
  for(unsigned slot=0;slot<2;++slot) {
    char path[144];checkpointPath(path,directory,service,chunk,slot);StaticJsonDocument<1024> doc;
    StorageLock lock;existed=existed||SD.exists(path);File file=openFile(path,FILE_READ);if(!file)continue;
    const bool ok=file.size()<=1024&&!deserializeJson(doc,file);file.close();
    if(!ok||!doc["generation"].is<uint32_t>()||strcmp(doc["sha256"]|"",hash)||doc["source_bytes"].as<size_t>()!=bytes)continue;
    const char* state=doc["state"]|"";if(!*state||strlen(state)>=sizeof(result.state))continue;
    const uint32_t generation=doc["generation"];
    if(!found||int32_t(generation-result.generation)>0) {
      result.generation=generation;result.boot=doc["boot"]|0U;result.attempts=doc["attempts"]|0U;result.retryAt=doc["retry_at"]|0U;result.epoch=doc["epoch"]|0U;
      strlcpy(result.state,state,sizeof(result.state));strlcpy(result.job,doc["job"]|"",sizeof(result.job));found=true;
    }
  }
  if(existed&&!found)strlcpy(result.state,"uncertain",sizeof(result.state));
  return found;
}
bool saveCheckpoint(const char* directory,int service,int chunk,const char* hash,size_t bytes,Checkpoint& cp) {
  StaticJsonDocument<1024> doc;doc["schema"]=1;doc["sha256"]=hash;doc["source_bytes"]=bytes;
  doc["generation"]=cp.generation+1;doc["state"]=cp.state;doc["job"]=cp.job;
  doc["boot"]=cp.boot;doc["attempts"]=cp.attempts;doc["retry_at"]=cp.retryAt;
  doc["epoch"]=cp.epoch;
  char path[144];checkpointPath(path,directory,service,chunk,(cp.generation+1)&1U);
  bool ok;
  UploadWriter output;
  {StorageLock lock;
    if(!output.file.open(path,storage::OpenMode::Truncate,output.buffer,sizeof(output.buffer)))return false;
    const size_t expected=measureJson(doc);ok=serializeJson(doc,output.file)==expected;
    // Always close, even after a short write. No readback or network request may
    // treat a failed fflush/fsync/fclose as a committed checkpoint.
    ok=output.file.close()&&ok;
  }
  Checkpoint verified;ok=ok&&loadCheckpoint(directory,service,chunk,hash,bytes,verified)&&verified.generation==cp.generation+1&&
    !strcmp(verified.state,cp.state)&&!strcmp(verified.job,cp.job)&&verified.attempts==cp.attempts&&
    verified.boot==cp.boot&&verified.retryAt==cp.retryAt&&verified.epoch==cp.epoch;
  if(ok)cp=verified;return ok;
}
bool setState(const char* directory,int service,int chunk,const char* hash,size_t bytes,Checkpoint& cp,const char* state) {
  strlcpy(cp.state,state,sizeof(cp.state));return saveCheckpoint(directory,service,chunk,hash,bytes,cp);
}
struct Response {int status=0;bool bodySent=false,complete=false;char body[2048]{};};
bool writeHttp(esp_http_client_handle_t client,const char* data,size_t size,Response& response,uint32_t started) {
  size_t sent=0;while(sent<size&&!stopped()&&millis()-started<60000) {
    const int n=esp_http_client_write(client,data+sent,int(size-sent));if(n<=0)return false;
    sent+=size_t(n);response.bodySent=true;
  }
  return sent==size;
}
__attribute__((noinline)) Response requestHttp(const Work& work,int service,const char* csv,const char* filename,const char* job) {
  sampleUploadResources();
  Response response;const bool poll=job&&*job;char url[180];
  if(poll)snprintf(url,sizeof(url),"https://wdgwars.pl/api/v2/upload-job/%s",job);else strlcpy(url,endpoints[service],sizeof(url));
  esp_http_client_config_t settings{};settings.url=url;settings.timeout_ms=4000;settings.disable_auto_redirect=true;
  settings.crt_bundle_attach=esp_crt_bundle_attach;settings.buffer_size=2048;settings.buffer_size_tx=1024;
  settings.method=poll?HTTP_METHOD_GET:HTTP_METHOD_POST;
  esp_http_client_handle_t client=esp_http_client_init(&settings);if(!client)return response;
  esp_http_client_set_header(client,"User-Agent","LORA-Tunnel/" TUNNEL_VERSION);
  esp_http_client_set_header(client,"Accept","application/json");
  if(service==0) {
    char credentialsText[260],authorization[360];size_t written=0;
    snprintf(credentialsText,sizeof(credentialsText),"%s:%s",work.settings.wigle_name,work.settings.wigle_token);
    memcpy(authorization,"Basic ",6);mbedtls_base64_encode(reinterpret_cast<uint8_t*>(authorization+6),sizeof(authorization)-7,&written,
      reinterpret_cast<const uint8_t*>(credentialsText),strlen(credentialsText));authorization[written+6]=0;
    esp_http_client_set_header(client,"Authorization",authorization);memset(credentialsText,0,sizeof(credentialsText));memset(authorization,0,sizeof(authorization));
  } else esp_http_client_set_header(client,"X-API-Key",service==1?work.settings.wardrift_token:work.settings.wdgwars_token);
  char prefix[320]{},suffix[64]{};size_t bytes=0;
  if(!poll) {
    if(!fileSize(csv,bytes)){esp_http_client_cleanup(client);return response;}
    if(service==1){esp_http_client_set_header(client,"Content-Type","text/csv");esp_http_client_set_header(client,"X-File-Name",filename);}
    else {
      esp_http_client_set_header(client,"Content-Type","multipart/form-data; boundary=----LoraTunnelUploadBoundary");
      snprintf(prefix,sizeof(prefix),"------LoraTunnelUploadBoundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\nContent-Type: text/csv\r\n\r\n",filename);
      strlcpy(suffix,"\r\n------LoraTunnelUploadBoundary--\r\n",sizeof(suffix));
    }
  }
  const uint32_t started=millis();bool ok=!stopped()&&esp_http_client_open(client,int(bytes+strlen(prefix)+strlen(suffix)))==ESP_OK;
  sampleUploadResources();
  if(ok&&!poll) {
    ok=writeHttp(client,prefix,strlen(prefix),response,started);File file;
    {StorageLock lock;file=openFile(csv,FILE_READ);}if(!file)ok=false;
    char block[1024];size_t sent=0;
    while(ok&&sent<bytes) {
      size_t got;{StorageLock lock;got=file.read(reinterpret_cast<uint8_t*>(block),std::min(sizeof(block),bytes-sent));}
      if(!got){ok=false;break;}ok=writeHttp(client,block,got,response,started);sent+=got;
    }
    closeFile(file);if(ok)ok=writeHttp(client,suffix,strlen(suffix),response,started);
  }
  if(ok&&!stopped()&&esp_http_client_fetch_headers(client)>=0) {
    response.status=esp_http_client_get_status_code(client);size_t used=0;
    // esp_http_client removes chunk framing. Bounded reads handle both a
    // Content-Length body and chunked transfer; truncated JSON is never success.
    while(used<sizeof(response.body)-1&&!stopped()&&millis()-started<60000) {
      const int n=esp_http_client_read(client,response.body+used,sizeof(response.body)-1-used);
      if(n<0)break;if(n==0){response.complete=esp_http_client_is_complete_data_received(client);break;}used+=size_t(n);
    }
    response.body[used]=0;if(esp_http_client_is_complete_data_received(client))response.complete=true;
  }
  sampleUploadResources();
  esp_http_client_close(client);esp_http_client_cleanup(client);return response;
}
bool associate(const Work& work) {
  status("Connecting to configured home Wi-Fi");
  WiFi.persistent(false);WiFi.setAutoReconnect(false);WiFi.mode(WIFI_STA);WiFi.begin(work.settings.wifi_name,work.settings.wifi_password);
  const uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<20000)if(!delayUntil(100))return false;
  if(WiFi.status()!=WL_CONNECTED){status("Home Wi-Fi unavailable; uploads deferred");return false;}
  // Certificate validation always remains enabled; establish wall time first.
  if(time(nullptr)<1735689600) {
    configTime(0,0,"time.cloudflare.com","pool.ntp.org");const uint32_t at=millis();
    while(time(nullptr)<1735689600&&millis()-at<15000)if(!delayUntil(100))return false;
  }
  if(time(nullptr)<1735689600){status("Clock unavailable; TLS upload deferred");return false;}return !stopped();
}
__attribute__((noinline)) bool processChunk(Work& work,const char* directory,const char* hash,size_t sourceBytes,unsigned chunk,bool empty) {
  char filename[100];snprintf(filename,sizeof(filename),"LORA-Tunnel-%.64s-%u.csv",hash,chunk);
  for(int service=0;service<3&&!stopped();++service) {
    if(!(work.mask&(1U<<service)))continue;
    const bool manual=work.manual&(1U<<service);
    if(!credentials(work.settings,service)){workServiceStatus(work,service,"configure service credentials");continue;}
    if(authBlocked.load()&(1U<<service)){workServiceStatus(work,service,"authentication paused; check API key");continue;}
    Checkpoint cp,budget;loadCheckpoint(directory,service,int(chunk),hash,sourceBytes,cp);
    loadCheckpoint(directory,service,-1,hash,sourceBytes,budget);
    if(!strcmp(cp.state,"accepted")||!strcmp(cp.state,"skipped")){work.alreadyProcessed=true;continue;}
    const bool polling=(!strcmp(cp.state,"job")||!strcmp(cp.state,"authjob")) && upload::safeJobId(cp.job);
    // A continuation worker cannot turn missing, stale or rejected jobs into POSTs.
    if((work.pollOnly||(work.pollMask&(1U<<service)))&&!polling)continue;
    if(empty){if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,"skipped")){workStatus(work,"SD checkpoint failed; originals kept");return false;}continue;}
    if((!strcmp(cp.state,"auth")||!strcmp(cp.state,"authjob")) && cp.epoch==work.epoch && !manual) {authBlocked.fetch_or(1U<<service);workServiceStatus(work,service,"authentication paused; check API key");continue;}
    // FAT iteration may discover a new alternating slot written earlier in this
    // pass. Persist the per-job due time so that cannot cause an immediate GET.
    if(polling&&!manual&&cp.boot==bootId&&cp.retryAt&&int32_t(millis()-cp.retryAt)<0){
      scheduleJobPoll(service,cp.retryAt-millis());workServiceStatus(work,service,"job queued; receipt pending");continue;}
    if(budget.boot!=bootId||budget.epoch!=work.epoch){budget.boot=bootId;budget.attempts=0;budget.retryAt=0;}
    budget.epoch=cp.epoch=work.epoch;
    if(budget.attempts>=3){workServiceStatus(work,service,"retry limit; manual retry available");continue;}
    if(budget.retryAt && int32_t(millis()-budget.retryAt)<0){
      if(polling)scheduleJobPoll(service,budget.retryAt-millis());else scheduleRetry(service,budget.retryAt-millis());
      workServiceStatus(work,service,polling?"job retry scheduled":"retry scheduled");continue;}
    if(!polling&&!upload::checkpointMaySend(cp.state,manual)){workServiceStatus(work,service,"receipt uncertain; manual retry needed");continue;}
    if(!polling&&!setState(directory,service,int(chunk),hash,sourceBytes,cp,"sending")){workStatus(work,"SD checkpoint failed; upload cancelled");return false;}
    workServiceStatus(work,service,polling?"checking persisted upload job":"sending completed session chunk");
    const Response response=requestHttp(work,service,staging,filename,polling?cp.job:nullptr);
    if(stopped()) {
      setState(directory,service,int(chunk),hash,sourceBytes,cp,polling?"job":response.bodySent?"uncertain":"retry");
      return false;
    }
    StaticJsonDocument<2048> body;const bool valid=response.complete&&!deserializeJson(body,response.body);
    const auto result=upload::receipt(service,response.status,valid?body.as<JsonVariantConst>():JsonVariantConst{},polling);
    if(result==upload::Receipt::Accepted) {
      if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,"accepted")){workStatus(work,"Receipt received; SD checkpoint failed");return false;}
      workServiceStatus(work,service,"accepted; original log retained");
    } else if(result==upload::Receipt::JobPending) {
      if(!polling)upload::normalizeJobId(body["job_id"],cp.job,sizeof(cp.job));
      cp.boot=bootId;cp.retryAt=millis()+30000;
      if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,"job")){workStatus(work,"Job accepted; SD checkpoint failed");return false;}
      scheduleJobPoll(service,30000);workServiceStatus(work,service,"job queued; receipt pending");
    } else {
      ++budget.attempts;budget.retryAt=millis()+upload::retryDelay(budget.attempts);
      if(!saveCheckpoint(directory,service,-1,hash,sourceBytes,budget)){workStatus(work,"SD retry checkpoint failed");return false;}
      const bool uncertain=response.status==0&&response.bodySent&&!polling;
      const char* state=result==upload::Receipt::JobFailed?"rejected":result==upload::Receipt::Authentication?(polling?"authjob":"auth"):polling?"job":uncertain?"uncertain":result==upload::Receipt::Retryable?"retry":"rejected";
      if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,state)){workStatus(work,"SD result checkpoint failed");return false;}
      if(result==upload::Receipt::Authentication){authBlocked.fetch_or(1U<<service);workServiceStatus(work,service,"authentication failed; check API key");}
      else if(uncertain)workServiceStatus(work,service,"receipt uncertain; manual retry needed");
      else if(result==upload::Receipt::Retryable && budget.attempts<3){
        if(polling)scheduleJobPoll(service,upload::retryDelay(budget.attempts));else scheduleRetry(service,upload::retryDelay(budget.attempts));
        workServiceStatus(work,service,"temporary failure; retry scheduled");}
      else workServiceStatus(work,service,result==upload::Receipt::JobFailed?"server job failed; original kept":"upload rejected; manual retry available");
    }
  }
  return true;
}
bool writeCsv(UploadWriter& output,const char* data,size_t size){
  StorageLock lock;
  const size_t written=output.file.write(reinterpret_cast<const uint8_t*>(data),size);
  output.written+=written;
  return written==size&&!output.file.failed();
}
bool stageHeader(UploadWriter& file) {
  {StorageLock lock;file.written=0;
    if(!file.file.open(staging,storage::OpenMode::Truncate,file.buffer,sizeof(file.buffer)))return false;}
  char first[240];
  snprintf(first,sizeof(first),"WigleWifi-1.6,appRelease=LORA-Tunnel-%s,model=%s,release=%s,device=LORA-Tunnel,display=keyboard,board=%s,brand=Valleytech\n",TUNNEL_VERSION,board::TARGET_ID,TUNNEL_VERSION,board::TARGET_ID);
  return writeCsv(file,first,strlen(first))&&writeCsv(file,upload::CsvColumns,strlen(upload::CsvColumns));
}
bool finishCsv(UploadWriter& output){StorageLock lock;return output && output.file.close();}
bool checkpointChunk(const char* name,unsigned& chunk,unsigned& slot) {
  if(strncmp(name,"2-",2))return false;
  const char* p=name+2;if(*p<'0'||*p>'9'||(*p=='0'&&p[1]>='0'&&p[1]<='9'))return false;
  uint32_t value=0;
  do {const unsigned digit=unsigned(*p-'0');if(value>(0x7fffffffU-digit)/10)return false;
    value=value*10+digit;++p;}while(*p>='0'&&*p<='9');
  if(!strcmp(p,".a.json"))slot=0;else if(!strcmp(p,".b.json"))slot=1;else return false;
  chunk=value;return true;
}
void pollSessionJobs(Work& work,const char* directory,const char* hash,size_t bytes) {
  if(!(work.mask&(1U<<2)))return;
  File jobs;{StorageLock lock;jobs=openFile(directory);}if(!jobs)return;
  while(!stopped()) {
    char name[100]{};bool file=false;
    {StorageLock lock;File entry=jobs.openNextFile();if(!entry)break;file=!entry.isDirectory();
      const char* base=strrchr(entry.name(),'/');strlcpy(name,base?base+1:entry.name(),sizeof(name));entry.close();}
    unsigned chunk=0,slot=0;if(!file||!checkpointChunk(name,chunk,slot))continue;
    // Process each chunk once even when both alternating slots exist. The
    // loader still validates both and chooses the newest valid generation.
    if(!slot){char other[144];checkpointPath(other,directory,2,int(chunk),1);
      StorageLock lock;if(SD.exists(other))continue;}
    Checkpoint cp;if(!loadCheckpoint(directory,2,int(chunk),hash,bytes,cp))continue;
    if((strcmp(cp.state,"job")&&strcmp(cp.state,"authjob"))||!upload::safeJobId(cp.job))continue;
    if(!processChunk(work,directory,hash,bytes,chunk,false))break;
  }
  closeFile(jobs);
}
__attribute__((noinline)) void processSession(Work& work,const char* path) {
  ParserStorage parser;
  if(!parser.acquire(false)){workStatus(work,"Upload memory unavailable; originals kept");return;}
  char hash[65];size_t bytes=0;if(!closedSession(path,hash,bytes,parser.line,*parser.doc))return;
  char directory[100];snprintf(directory,sizeof(directory),"%s/%s",root,hash);
  if(work.pollOnly){parser.release();pollSessionJobs(work,directory,hash,bytes);return;}
  {StorageLock lock;if(!SD.mkdir(directory)){workStatus(work,"Cannot create upload checkpoints on SD");return;}}
  bool alreadyComplete=true;
  for(int service=0;service<3;++service)if(work.mask&(1U<<service)) {
    Checkpoint complete;loadCheckpoint(directory,service,-2,hash,bytes,complete);
    if(strcmp(complete.state,"accepted")&&strcmp(complete.state,"skipped"))alreadyComplete=false;
  }
  if(alreadyComplete){work.alreadyProcessed=true;return;}
  // Manual requests reset the per-source/service retry budget without clearing
  // accepted chunks or persisted asynchronous jobs.
  for(int service=0;service<3;++service)if(work.manual&(1U<<service)) {
    Checkpoint budget;loadCheckpoint(directory,service,-1,hash,bytes,budget);budget.boot=bootId;budget.attempts=0;budget.retryAt=0;budget.epoch=work.epoch;
    if(!saveCheckpoint(directory,service,-1,hash,bytes,budget)){workStatus(work,"SD checkpoint failed");return;}
  }
  if(!parser.acquire(true)){workStatus(work,"Upload memory unavailable; originals kept");return;}
  File source;UploadWriter out;{StorageLock lock;source=openFile(path,FILE_READ);}if(!source){workStatus(work,"Cannot read completed session on SD");return;}
  if(!stageHeader(out)){closeFile(source);closeFile(out);workStatus(work,"Cannot stage upload CSV on SD");return;}
  unsigned chunk=0,rows=0;size_t chunkBytes;{StorageLock lock;chunkBytes=out.size();}
  bool ok=true;
  while(!stopped()) {
    size_t rowStart;{StorageLock lock;rowStart=source.position();}
    const int count=readLine(source,parser.line,LineCapacity);if(count==0)break;if(count==-2){ok=false;break;}if(count<0)continue;
    parser.doc->clear();if(deserializeJson(*parser.doc,parser.line))continue;
    const size_t length=upload::csvRow(parser.doc->as<JsonVariantConst>(),parser.row,upload::CsvRowCapacity);if(!length)continue;
    if(chunkBytes+length>upload::CsvChunkLimit) {
      if(!finishCsv(out)){ok=false;break;}
      closeFile(source);parser.release();
      if(!processChunk(work,directory,hash,bytes,chunk,false)){ok=false;break;}if(stopped())break;
      if(!parser.acquire(true)){workStatus(work,"Upload memory unavailable; originals kept");return;}
      // The overflow row belongs to the next chunk. Reopen at its original
      // offset and parse it again, retaining no row or JSON arena during TLS.
      {StorageLock lock;source=openFile(path,FILE_READ);ok=source&&source.size()==bytes&&source.seek(rowStart);}
      if(!ok)break;
      ++chunk;rows=0;if(!stageHeader(out)){ok=false;break;}chunkBytes=out.size();
      continue;
    }
    if(!writeCsv(out,parser.row,length)){ok=false;break;}chunkBytes+=length;++rows;
    taskYIELD();
  }
  closeFile(source);parser.release();
  if(ok&&!stopped()&&finishCsv(out)) {
    if(chunk==0&&rows==0)work.skippedFiles=true;
    if(!processChunk(work,directory,hash,bytes,chunk,rows==0))return;
    // A compact whole-file completion checkpoint avoids rewriting derived CSV
    // on every idle scan. Source SHA256 is still rechecked before trusting it.
    for(int service=0;service<3;++service)if(work.mask&(1U<<service)) {
      bool complete=true,accepted=false;for(unsigned part=0;part<=chunk;++part) {
        Checkpoint state;loadCheckpoint(directory,service,int(part),hash,bytes,state);
        if(!strcmp(state.state,"accepted"))accepted=true;
        else if(strcmp(state.state,"skipped")){complete=false;break;}
      }
      if(complete){Checkpoint summary;loadCheckpoint(directory,service,-2,hash,bytes,summary);if(!setState(directory,service,-2,hash,bytes,summary,accepted?"accepted":"skipped")){workStatus(work,"SD completion checkpoint failed");return;}}
    }
  }
  else {closeFile(out);if(!stopped())workStatus(work,"SD staging error; original session kept");}
}
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
std::atomic<bool> workerFinished{false};
#endif
void worker(void* argument) {
  Work* work=static_cast<Work*>(argument);
  const bool radioReady=true; // Cardputer's coordinator released BLE first.
  sampleUploadResources();
  bool ok=radioReady&&!stopped();
  if(work->selfCheck) {
    {ParserStorage parser;ok=ok&&parser.acquire(true);}
    sampleUploadResources();
    if(ok)ok=delayUntil(50);
    selfCheckState=ok?2:3;
    status(ok?"Offline upload memory check passed":"Offline upload memory check failed");
  } else {
    if(ok) {StorageLock lock;ok=SD.mkdir(root);if(!ok)status("Upload staging unavailable");}
    if(ok)ok=associate(*work);
    if(ok) {
      associationFailures=0;
      File directory;{StorageLock lock;directory=openFile("/sessions");}
      if(!directory)status("No completed sessions available");
      while(directory&&!stopped()) {
        char path[100]{};bool isFile=false;
        {StorageLock lock;File entry=directory.openNextFile();if(!entry)break;
          isFile=!entry.isDirectory();const char* name=entry.name();const char* base=strrchr(name,'/');base=base?base+1:name;
          if(strlen(base)<70)snprintf(path,sizeof(path),"/sessions/%s",base);entry.close();}
        const size_t n=strlen(path);
        if(isFile&&n>6&&!strcmp(path+n-6,".jsonl"))processSession(*work,path);
      }
      closeFile(directory);
      if(!stopped()&&!work->resultReported) {
        status(work->pollOnly?"No pending upload jobs":work->alreadyProcessed&&!work->skippedFiles?
          "No new data; already processed":"No eligible data; nothing uploaded");
      }
    } else if(radioReady&&!stopped()) {
      const unsigned failures=associationFailures.fetch_add(1)+1;
      if(failures<3)for(int service=0;service<3;++service)if(work->mask&(1U<<service)){
        if(work->pollOnly||(work->pollMask&(1U<<service)))scheduleJobPoll(service,upload::retryDelay(failures));else scheduleRetry(service,upload::retryDelay(failures));}
    }
    WiFi.disconnect(false,false);
  }
  if(stopped())status("Upload cancelled; local logs retained");
  sampleUploadResources();++uploadRuns;
  memset(&work->settings,0,sizeof(work->settings));delete work;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  // The existing writer is pinned to this same core. Park after every resource
  // is gone, so it can delete a non-running task and reclaim this entire stack
  // synchronously before restoring BLE. Never self-delete or release busy here.
  workerFinished=true;
  for(;;) vTaskSuspend(nullptr);
#else
  // A cancellation can arrive while a checkpoint write is in progress, before
  // that path re-arms a retry. After all scheduling and restoration have ended,
  // clear any such retry while busy still prevents a new worker/reset. This
  // sequentially consistent read orders against uploaderCancel: a later cancel
  // clears the state itself, with no worker scheduling left to race against it.
  if(cancelled.load()) { retryMask=jobPollMask=0;nextRetry=nextJobPoll=0; }
  busy=false;vTaskDelete(nullptr);
#endif
}
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
void finishMaintenance() {
  // No worker remains that can re-arm work after cancellation, including a
  // cancellation that arrived during the final checkpoint or BLE restore.
  if(cancelled.load()) {retryMask=jobPollMask=0;nextRetry=nextJobPoll=0;}
  busy=false;
}
void uploadMaintenance(void* argument) {
  Work* work=static_cast<Work*>(argument);
  const bool checking=work->selfCheck;
  bool paused=false;
  if(!stopped()) {
    status("Releasing BLE memory for upload");
    paused=scannersPauseBleForUpload();
    if(!paused) status("BLE pause failed; upload deferred");
  }
  if(paused && !stopped()) {
    TaskHandle_t task=nullptr;workerFinished=false;
    if(xTaskCreatePinnedToCore(worker,"upload",16384,work,1,&task,0)==pdPASS) {
      // Worker and coordinator share core 0. Waiting for the explicit suspended
      // state makes vTaskDelete synchronous, not an idle-task cleanup request.
      work=nullptr;
      while(!workerFinished.load() || eTaskGetState(task)!=eSuspended) vTaskDelay(1);
      vTaskDelete(task);
    } else status("Upload worker unavailable");
  }
  if(work) {memset(&work->settings,0,sizeof(work->settings));delete work;}
  if(stopped()) status("Upload cancelled; local logs retained");
  if(paused) {
    char completion[sizeof(statusText)];uploaderStatus(completion,sizeof(completion));
    status("Restoring BLE scanner");
    if(scannersRestoreBleAfterUpload())status(completion);
    else {status("BLE restore failed; restart before capture");if(checking)selfCheckState=3;}
  }
  if(checking&&(stopped()||selfCheckState.load()!=2))selfCheckState=3;
}
#endif
}
UploadMetrics uploaderMetrics(){return {uploadRuns.load(),uploadMinFree.load(),uploadSmallestBlock.load(),uploadStackMargin.load(),selfCheckState.load()};}
bool uploaderSelfCheck() {
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  if(busy || !loggerIdle() || !scannersCaptureReady())return false;
  Work* work=new(std::nothrow) Work;if(!work)return false;
  uploaderCancel();work->selfCheck=true;cancelled=false;busy=true;selfCheckState=1;
  if(loggerRequestMaintenance(uploadMaintenance,work,finishMaintenance))return true;
  delete work;busy=false;selfCheckState=3;return false;
#else
  return false;
#endif
}
bool uploaderBegin(){bootId=esp_random();initialPending=true;Preferences prefs;if(prefs.begin("lt-upload",true)){settingsEpoch=prefs.getUInt("epoch",1);prefs.end();}return true;}
bool uploaderBusy(){return busy.load();}
void uploaderCancel(){
  cancelled=true;requests=0;retryMask=jobPollMask=0;nextRetry=nextJobPoll=0;initialPending=false;
  // Consume this idle-entry trigger even if a settings page delayed automatic
  // scheduling. A subsequent capture/non-idle tick enables the next idle entry.
  wasIdle=true;
}
void uploaderStatus(char* destination,size_t capacity){if(!destination||!capacity)return;portENTER_CRITICAL(&statusMux);strlcpy(destination,statusText,capacity);portEXIT_CRITICAL(&statusMux);}
void uploaderConfigChanged(){
  Preferences prefs;if(prefs.begin("lt-upload",false)){const uint32_t next=settingsEpoch+1;if(prefs.putUInt("epoch",next)==sizeof(next))settingsEpoch=next;prefs.end();}
  authBlocked=0;associationFailures=0;initialPending=true;retryMask=jobPollMask=0;nextRetry=nextJobPoll=0;
}
bool uploaderRequest(int service) {
  if(service< -1||service>2||busy||!storageReady())return false;
  unsigned mask=service<0?enabled(config):(1U<<service);unsigned valid=0;
  for(int i=0;i<3;++i)if((mask&(1U<<i))&&credentials(config,i))valid|=1U<<i;
  if(!valid){status("Configure home Wi-Fi and service API keys");return false;}
  associationFailures=0;authBlocked.fetch_and(~valid);requests.fetch_or(valid);return true;
}
void uploaderTick(bool idle,bool allowAutomatic,bool allowJobPolling) {
  if(!idle){wasIdle=false;return;}if(busy)return;
  const unsigned manual=requests.exchange(0);unsigned mask=manual;
  if(allowAutomatic) {
    if(!wasIdle||initialPending){mask|=enabled(config);initialPending=false;}
    const uint32_t due=nextRetry.load();if(due&&int32_t(millis()-due)>=0){mask|=retryMask.exchange(0);nextRetry=0;}
    wasIdle=true;
  }
  // Job GETs may continue on the Uploads status page, but editing/Power and
  // non-idle capture gates retain pending work without launching any network.
  unsigned polls=0;const uint32_t pollDue=nextJobPoll.load();
  if((allowAutomatic||allowJobPolling)&&pollDue&&int32_t(millis()-pollDue)>=0){polls=jobPollMask.exchange(0);nextJobPoll=0;}
  const bool pollOnly=!mask&&polls;const unsigned pollMask=polls&~mask;mask|=polls;
  mask&=~authBlocked.load();
  for(int i=0;i<3;++i)if((mask&(1U<<i))&&!credentials(config,i))mask&=~(1U<<i);
  if(!mask||!storageReady())return;
  Work* work=new(std::nothrow) Work;if(!work){status("Upload task allocation failed");return;}
  work->settings=config;work->mask=mask;work->manual=manual;work->epoch=settingsEpoch;work->pollOnly=pollOnly;work->pollMask=pollMask;cancelled=false;busy=true;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  if(!loggerRequestMaintenance(uploadMaintenance,work,finishMaintenance)) {
    busy=false;memset(&work->settings,0,sizeof(work->settings));delete work;status("Upload maintenance unavailable");
  }
#else
  if(xTaskCreatePinnedToCore(worker,"upload",16384,work,1,nullptr,0)!=pdPASS){busy=false;memset(&work->settings,0,sizeof(work->settings));delete work;status("Upload worker unavailable");}
#endif
}
}
