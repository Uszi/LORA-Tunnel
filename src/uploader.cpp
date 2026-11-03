#include "uploader.h"
#include "upload_csv.h"
#include "upload_protocol.h"
#include "credential_validation.h"
#include "config.h"
#include "app_types.h"
#include "logger.h"
#include "board_config.h"
#include "version.h"
#include <SD.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <esp_system.h>
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
std::atomic<uint32_t> nextRetry{0};
bool wasIdle=false,initialPending=true;
uint32_t bootId=0,settingsEpoch=1;
std::atomic<unsigned> associationFailures{0};
portMUX_TYPE statusMux=portMUX_INITIALIZER_UNLOCKED;
char statusText[64]="Uploads off; configure keys and services";
struct Work { Config settings; unsigned mask=0,manual=0; uint32_t epoch=1; };
struct StorageLock {StorageLock(){xSemaphoreTake(busMutex,portMAX_DELAY);}~StorageLock(){xSemaphoreGive(busMutex);}};
void status(const char* text){portENTER_CRITICAL(&statusMux);strlcpy(statusText,text,sizeof(statusText));portEXIT_CRITICAL(&statusMux);}
void serviceStatus(int service,const char* text){char line[64];snprintf(line,sizeof(line),"%s: %s",names[service],text);status(line);}
bool stopped(){return cancelled.load(std::memory_order_relaxed);}
bool delayUntil(uint32_t duration){const uint32_t start=millis();while(millis()-start<duration){if(stopped())return false;vTaskDelay(pdMS_TO_TICKS(50));}return true;}
void closeFile(File& file){StorageLock lock;if(file)file.close();}
bool fileSize(const char* path,size_t& size){StorageLock lock;File file=SD.open(path,FILE_READ);if(!file)return false;size=file.size();file.close();return true;}
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
bool closedSession(const char* path,char digest[65],size_t& bytes,char* buffer,DynamicJsonDocument& doc) {
  File file;{StorageLock lock;file=SD.open(path,FILE_READ);if(!file)return false;bytes=file.size();}
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
    StorageLock lock;existed=existed||SD.exists(path);File file=SD.open(path,FILE_READ);if(!file)continue;
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
  {StorageLock lock;File file=SD.open(path,FILE_WRITE);if(!file)return false;
    const size_t expected=measureJson(doc);ok=serializeJson(doc,file)==expected;file.flush();ok=ok&&!file.getWriteError();file.close();}
  Checkpoint verified;ok=ok&&loadCheckpoint(directory,service,chunk,hash,bytes,verified)&&verified.generation==cp.generation+1&&
    !strcmp(verified.state,cp.state)&&!strcmp(verified.job,cp.job)&&verified.attempts==cp.attempts;
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
Response requestHttp(const Work& work,int service,const char* csv,const char* filename,const char* job) {
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
  if(ok&&!poll) {
    ok=writeHttp(client,prefix,strlen(prefix),response,started);File file;
    {StorageLock lock;file=SD.open(csv,FILE_READ);}if(!file)ok=false;
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
bool processChunk(const Work& work,const char* directory,const char* hash,size_t sourceBytes,unsigned chunk,bool empty) {
  char filename[100];snprintf(filename,sizeof(filename),"LORA-Tunnel-%.64s-%u.csv",hash,chunk);
  for(int service=0;service<3&&!stopped();++service) {
    if(!(work.mask&(1U<<service)))continue;
    const bool manual=work.manual&(1U<<service);
    if(!credentials(work.settings,service)){serviceStatus(service,"configure service credentials");continue;}
    if(authBlocked.load()&(1U<<service))continue;
    Checkpoint cp,budget;loadCheckpoint(directory,service,int(chunk),hash,sourceBytes,cp);
    loadCheckpoint(directory,service,-1,hash,sourceBytes,budget);
    if(!strcmp(cp.state,"accepted")||!strcmp(cp.state,"skipped"))continue;
    if(empty){if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,"skipped")){status("SD checkpoint failed; originals kept");return false;}continue;}
    if((!strcmp(cp.state,"auth")||!strcmp(cp.state,"authjob")) && cp.epoch==work.epoch && !manual) {authBlocked.fetch_or(1U<<service);serviceStatus(service,"authentication paused; check API key");continue;}
    if(budget.boot!=bootId||budget.epoch!=work.epoch){budget.boot=bootId;budget.attempts=0;budget.retryAt=0;}
    budget.epoch=cp.epoch=work.epoch;
    if(budget.attempts>=3){serviceStatus(service,"retry limit; manual retry available");continue;}
    if(budget.retryAt && int32_t(millis()-budget.retryAt)<0){scheduleRetry(service,budget.retryAt-millis());continue;}
    const bool polling=(!strcmp(cp.state,"job")||!strcmp(cp.state,"authjob")) && upload::safeJobId(cp.job);
    if(!polling&&!upload::checkpointMaySend(cp.state,manual)){serviceStatus(service,"receipt uncertain; manual retry needed");continue;}
    if(!polling&&!setState(directory,service,int(chunk),hash,sourceBytes,cp,"sending")){status("SD checkpoint failed; upload cancelled");return false;}
    serviceStatus(service,polling?"checking persisted upload job":"sending completed session chunk");
    const Response response=requestHttp(work,service,staging,filename,polling?cp.job:nullptr);
    if(stopped()) {
      setState(directory,service,int(chunk),hash,sourceBytes,cp,polling?"job":response.bodySent?"uncertain":"retry");
      return false;
    }
    StaticJsonDocument<2048> body;const bool valid=response.complete&&!deserializeJson(body,response.body);
    const auto result=upload::receipt(service,response.status,valid?body.as<JsonVariantConst>():JsonVariantConst{},polling);
    if(result==upload::Receipt::Accepted) {
      if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,"accepted")){status("Receipt received; SD checkpoint failed");return false;}
      serviceStatus(service,"accepted; original log retained");
    } else if(result==upload::Receipt::JobPending) {
      if(!polling)upload::normalizeJobId(body["job_id"],cp.job,sizeof(cp.job));
      if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,"job")){status("Job accepted; SD checkpoint failed");return false;}
      scheduleRetry(service,30000);serviceStatus(service,"job queued; receipt pending");
    } else {
      ++budget.attempts;budget.retryAt=millis()+upload::retryDelay(budget.attempts);
      if(!saveCheckpoint(directory,service,-1,hash,sourceBytes,budget)){status("SD retry checkpoint failed");return false;}
      const bool uncertain=response.status==0&&response.bodySent&&!polling;
      const char* state=result==upload::Receipt::JobFailed?"rejected":result==upload::Receipt::Authentication?(polling?"authjob":"auth"):polling?"job":uncertain?"uncertain":result==upload::Receipt::Retryable?"retry":"rejected";
      if(!setState(directory,service,int(chunk),hash,sourceBytes,cp,state)){status("SD result checkpoint failed");return false;}
      if(result==upload::Receipt::Authentication){authBlocked.fetch_or(1U<<service);serviceStatus(service,"authentication failed; check API key");}
      else if(uncertain)serviceStatus(service,"receipt uncertain; manual retry needed");
      else if(result==upload::Receipt::Retryable && budget.attempts<3){scheduleRetry(service,upload::retryDelay(budget.attempts));serviceStatus(service,"temporary failure; retry scheduled");}
      else serviceStatus(service,result==upload::Receipt::JobFailed?"server job failed; original kept":"upload rejected; manual retry available");
    }
  }
  return true;
}
bool writeCsv(File& file,const char* data,size_t size){StorageLock lock;return file.write(reinterpret_cast<const uint8_t*>(data),size)==size&&!file.getWriteError();}
bool stageHeader(File& file) {
  {StorageLock lock;if(SD.exists(staging))SD.remove(staging);file=SD.open(staging,FILE_WRITE);}
  if(!file)return false;char first[240];
  snprintf(first,sizeof(first),"WigleWifi-1.6,appRelease=LORA-Tunnel-%s,model=%s,release=%s,device=LORA-Tunnel,display=keyboard,board=%s,brand=Valleytech\n",TUNNEL_VERSION,board::TARGET_ID,TUNNEL_VERSION,board::TARGET_ID);
  return writeCsv(file,first,strlen(first))&&writeCsv(file,upload::CsvColumns,strlen(upload::CsvColumns));
}
bool finishCsv(File& file){StorageLock lock;if(!file)return false;file.flush();const bool ok=!file.getWriteError();file.close();return ok;}
void processSession(const Work& work,const char* path,char* line,char* row,DynamicJsonDocument& doc) {
  char hash[65];size_t bytes=0;if(!closedSession(path,hash,bytes,line,doc))return;
  char directory[100];snprintf(directory,sizeof(directory),"%s/%s",root,hash);
  {StorageLock lock;if(!SD.mkdir(directory)){status("Cannot create upload checkpoints on SD");return;}}
  bool alreadyComplete=true;
  for(int service=0;service<3;++service)if(work.mask&(1U<<service)) {
    Checkpoint complete;loadCheckpoint(directory,service,-2,hash,bytes,complete);
    if(strcmp(complete.state,"accepted"))alreadyComplete=false;
  }
  if(alreadyComplete)return;
  // Manual requests reset the per-source/service retry budget without clearing
  // accepted chunks or persisted asynchronous jobs.
  for(int service=0;service<3;++service)if(work.manual&(1U<<service)) {
    Checkpoint budget;loadCheckpoint(directory,service,-1,hash,bytes,budget);budget.boot=bootId;budget.attempts=0;budget.retryAt=0;budget.epoch=work.epoch;
    if(!saveCheckpoint(directory,service,-1,hash,bytes,budget)){status("SD checkpoint failed");return;}
  }
  File source,out;{StorageLock lock;source=SD.open(path,FILE_READ);}if(!source)return;
  if(!stageHeader(out)){closeFile(source);closeFile(out);status("Cannot stage upload CSV on SD");return;}
  unsigned chunk=0,rows=0;size_t chunkBytes;{StorageLock lock;chunkBytes=out.size();}
  bool ok=true;
  while(!stopped()) {
    const int count=readLine(source,line,LineCapacity);if(count==0)break;if(count==-2){ok=false;break;}if(count<0)continue;
    doc.clear();if(deserializeJson(doc,line))continue;
    const size_t length=upload::csvRow(doc.as<JsonVariantConst>(),row,upload::CsvRowCapacity);if(!length)continue;
    if(chunkBytes+length>upload::CsvChunkLimit) {
      if(!finishCsv(out)){ok=false;break;}
      if(!processChunk(work,directory,hash,bytes,chunk,false)){ok=false;break;}if(stopped())break;
      ++chunk;rows=0;if(!stageHeader(out)){ok=false;break;}{StorageLock lock;chunkBytes=out.size();}
    }
    if(!writeCsv(out,row,length)){ok=false;break;}chunkBytes+=length;++rows;
    taskYIELD();
  }
  closeFile(source);
  if(ok&&!stopped()&&finishCsv(out)) {
    if(!processChunk(work,directory,hash,bytes,chunk,rows==0))return;
    // A compact whole-file completion checkpoint avoids rewriting derived CSV
    // on every idle scan. Source SHA256 is still rechecked before trusting it.
    for(int service=0;service<3;++service)if(work.mask&(1U<<service)) {
      bool complete=true;for(unsigned part=0;part<=chunk;++part) {
        Checkpoint state;loadCheckpoint(directory,service,int(part),hash,bytes,state);
        if(strcmp(state.state,"accepted")&&strcmp(state.state,"skipped")){complete=false;break;}
      }
      if(complete){Checkpoint summary;loadCheckpoint(directory,service,-2,hash,bytes,summary);if(!setState(directory,service,-2,hash,bytes,summary,"accepted")){status("SD completion checkpoint failed");return;}}
    }
  }
  else {closeFile(out);if(!stopped())status("SD staging error; original session kept");}
}
void worker(void* argument) {
  Work* work=static_cast<Work*>(argument);
  char* line=static_cast<char*>(malloc(LineCapacity));char* row=static_cast<char*>(malloc(upload::CsvRowCapacity));
  DynamicJsonDocument* doc=new(std::nothrow) DynamicJsonDocument(4096);
  bool ok=line&&row&&doc&&doc->capacity()>=4096;
  if(!ok)status("Upload memory unavailable; try after restart");
  if(ok){StorageLock lock;ok=SD.mkdir(root);}if(!ok)status("Upload staging unavailable");
  if(ok)ok=associate(*work);
  if(ok) {
    associationFailures=0;
    File directory;{StorageLock lock;directory=SD.open("/sessions");}
    if(!directory)status("No completed sessions available");
    while(directory&&!stopped()) {
      char path[100]{};bool isFile=false;
      {StorageLock lock;File entry=directory.openNextFile();if(!entry)break;
        isFile=!entry.isDirectory();const char* name=entry.name();const char* base=strrchr(name,'/');base=base?base+1:name;
        if(strlen(base)<70)snprintf(path,sizeof(path),"/sessions/%s",base);entry.close();}
      const size_t n=strlen(path);
      if(isFile&&n>6&&!strcmp(path+n-6,".jsonl"))processSession(*work,path,line,row,*doc);
    }
    closeFile(directory);
  } else if(!stopped()) {
    const unsigned failures=associationFailures.fetch_add(1)+1;
    if(failures<3)for(int service=0;service<3;++service)if(work->mask&(1U<<service))scheduleRetry(service,upload::retryDelay(failures));
  }
  WiFi.disconnect(false,false);
  if(stopped())status("Upload cancelled; local logs retained");
  free(line);free(row);delete doc;memset(&work->settings,0,sizeof(work->settings));delete work;
  busy=false;vTaskDelete(nullptr);
}
}
bool uploaderBegin(){bootId=esp_random();initialPending=true;Preferences prefs;if(prefs.begin("lt-upload",true)){settingsEpoch=prefs.getUInt("epoch",1);prefs.end();}return true;}
bool uploaderBusy(){return busy.load();}
void uploaderCancel(){cancelled=true;requests=0;retryMask=0;nextRetry=0;initialPending=false;}
void uploaderStatus(char* destination,size_t capacity){if(!destination||!capacity)return;portENTER_CRITICAL(&statusMux);strlcpy(destination,statusText,capacity);portEXIT_CRITICAL(&statusMux);}
void uploaderConfigChanged(){
  Preferences prefs;if(prefs.begin("lt-upload",false)){const uint32_t next=settingsEpoch+1;if(prefs.putUInt("epoch",next)==sizeof(next))settingsEpoch=next;prefs.end();}
  authBlocked=0;associationFailures=0;initialPending=true;retryMask=0;nextRetry=0;
}
bool uploaderRequest(int service) {
  if(service< -1||service>2||busy||!storageReady())return false;
  unsigned mask=service<0?enabled(config):(1U<<service);unsigned valid=0;
  for(int i=0;i<3;++i)if((mask&(1U<<i))&&credentials(config,i))valid|=1U<<i;
  if(!valid){status("Configure home Wi-Fi and service API keys");return false;}
  associationFailures=0;authBlocked.fetch_and(~valid);requests.fetch_or(valid);return true;
}
void uploaderTick(bool idle,bool allowAutomatic) {
  if(!idle){wasIdle=false;return;}if(busy)return;
  const unsigned manual=requests.exchange(0);unsigned mask=manual;
  if(allowAutomatic) {
    if(!wasIdle||initialPending){mask|=enabled(config);initialPending=false;}
    const uint32_t due=nextRetry.load();if(due&&int32_t(millis()-due)>=0){mask|=retryMask.exchange(0);nextRetry=0;}
    wasIdle=true;
  }
  mask&=~authBlocked.load();
  for(int i=0;i<3;++i)if((mask&(1U<<i))&&!credentials(config,i))mask&=~(1U<<i);
  if(!mask||!storageReady())return;
  Work* work=new(std::nothrow) Work;if(!work){status("Upload task allocation failed");return;}
  work->settings=config;work->mask=mask;work->manual=manual;work->epoch=settingsEpoch;cancelled=false;busy=true;
  if(xTaskCreatePinnedToCore(worker,"upload",16384,work,1,nullptr,0)!=pdPASS){busy=false;memset(&work->settings,0,sizeof(work->settings));delete work;status("Upload worker unavailable");}
}
}
