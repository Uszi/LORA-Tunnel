#include "checked_sd_file.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace tunnel::storage;
unsigned checks=0;
void check(bool value,const char* message) { ++checks;if(!value) throw std::runtime_error(message); }
namespace fake {
struct Node {std::string bytes;bool directory=false;};
struct Handle {Node* node;size_t pos=0;bool error=false,closed=false,writable=false;int fd;};
std::map<std::string,Node> nodes;
std::vector<std::unique_ptr<Handle>> handles;
bool requireLock=false,busHeld=false;
bool failOpen=false,failBuffer=false,failRead=false,failFlush=false,failSync=false,failClose=false;
bool shortWrite=false,fullWriteError=false,failStat=false,failFileStat=false,failDescriptor=false;
unsigned active=0,peak=0,opens=0,closes=0,flushes=0,syncs=0,writes=0,statCalls=0;
size_t largestWrite=0;int bufferMode=-1;void* bufferAddress=nullptr;size_t bufferBytes=0;
void lockCheck() {if(requireLock)check(busHeld,"I/O outside bus mutex");}
Handle& handle(FILE* file) {auto* h=reinterpret_cast<Handle*>(file);check(h&&!h->closed,"use after close");return *h;}
FILE* open(const char* path,OpenMode mode) {
  lockCheck();++opens;
  if(failOpen || active>=2) {errno=failOpen?EIO:EMFILE;return nullptr;}
  auto found=nodes.find(path);
  if(mode==OpenMode::Read && found==nodes.end()) {errno=ENOENT;return nullptr;}
  if(mode==OpenMode::Exclusive && found!=nodes.end()) {errno=EEXIST;return nullptr;}
  if(found!=nodes.end() && found->second.directory) {errno=EISDIR;return nullptr;}
  Node& node=nodes[path];if(mode!=OpenMode::Read)node.bytes.clear();
  handles.emplace_back(new Handle{&node,0,false,false,mode!=OpenMode::Read,int(handles.size()+3)});
  ++active;peak=std::max(peak,active);return reinterpret_cast<FILE*>(handles.back().get());
}
int setBuffer(FILE* file,char* buffer,int mode,size_t bytes) {lockCheck();handle(file);bufferMode=mode;bufferAddress=buffer;bufferBytes=bytes;if(failBuffer){errno=ENOMEM;return -1;}return 0;}
size_t read(void* dst,size_t unit,size_t count,FILE* file) {
  lockCheck();auto& h=handle(file);check(unit==1,"read byte units");
  if(failRead){h.error=true;errno=EIO;return 0;}
  const size_t n=std::min(count,h.node->bytes.size()-h.pos);std::memcpy(dst,h.node->bytes.data()+h.pos,n);h.pos+=n;return n;
}
size_t write(const void* src,size_t unit,size_t count,FILE* file) {
  lockCheck();auto& h=handle(file);check(unit==1&&h.writable,"write byte units");++writes;largestWrite=std::max(largestWrite,count);
  const size_t n=shortWrite?count/2:count;h.node->bytes.append(static_cast<const char*>(src),n);h.pos+=n;
  if(shortWrite||fullWriteError){h.error=true;errno=ENOSPC;}return n;
}
int error(FILE* file) {lockCheck();return handle(file).error;}
int flush(FILE* file) {lockCheck();auto& h=handle(file);++flushes;if(failFlush){h.error=true;errno=ENOSPC;return EOF;}return 0;}
int descriptor(FILE* file) {lockCheck();auto& h=handle(file);if(failDescriptor){errno=EBADF;return -1;}return h.fd;}
int sync(int) {lockCheck();++syncs;if(failSync){errno=EIO;return -1;}return 0;}
int close(FILE* file) {lockCheck();auto& h=handle(file);h.closed=true;--active;++closes;if(failClose){errno=EIO;return EOF;}return 0;}
int fileStat(int fd,struct stat* info) {lockCheck();if(failFileStat){errno=EIO;return -1;}for(auto& h:handles)if(h->fd==fd&&!h->closed){info->st_size=h->node->bytes.size();info->st_mode=S_IFREG;return 0;}errno=EBADF;return -1;}
int pathStat(const char* path,struct stat* info) {lockCheck();++statCalls;if(failStat){errno=EACCES;return -1;}auto i=nodes.find(path);if(i==nodes.end()){errno=ENOENT;return -1;}info->st_mode=i->second.directory?S_IFDIR:S_IFREG;info->st_size=i->second.bytes.size();return 0;}
int remove(const char* path) {lockCheck();if(!nodes.erase(path)){errno=ENOENT;return -1;}return 0;}
int rename(const char* from,const char* to) {lockCheck();auto i=nodes.find(from);if(i==nodes.end()){errno=ENOENT;return -1;}nodes[to]=i->second;nodes.erase(i);return 0;}
int mkdir(const char* path) {lockCheck();if(nodes.count(path)){errno=EEXIST;return -1;}nodes[path].directory=true;return 0;}
const Operations operations={open,setBuffer,read,write,error,flush,descriptor,sync,close,fileStat,pathStat,remove,rename,mkdir};
void reset() {
  check(active==0,"previous handle leaked");nodes.clear();handles.clear();requireLock=busHeld=false;
  failOpen=failBuffer=failRead=failFlush=failSync=failClose=shortWrite=fullWriteError=failStat=failFileStat=failDescriptor=false;
  active=peak=opens=closes=flushes=syncs=writes=statCalls=0;largestWrite=0;bufferMode=-1;bufferAddress=nullptr;bufferBytes=0;
}
}
namespace nativeOpenCheck {
int opened=0,closed=0;bool failFd=false,failStream=false;int flags=0;
int fdOpenFake(const char*,int value,int){++opened;flags=value;if(failFd){errno=ENOSPC;return -1;}return 17;}
FILE* streamOpenFake(const char*,const char*){return reinterpret_cast<FILE*>(1);}
FILE* fdStreamFake(int,const char*){if(failStream){errno=ENOMEM;return nullptr;}return reinterpret_cast<FILE*>(1);}
int fdCloseFake(int fd){check(fd==17,"cleanup exact descriptor");++closed;errno=EBADF;return -1;}
// NATIVE_OPEN_IMPLEMENTATION
void run(){
 failStream=true;errno=0;check(nativeOpen("/sd/a",OpenMode::Exclusive)==nullptr,"fdopen fails");check(opened==1&&closed==1&&errno==ENOMEM,"fdopen cleanup preserves first errno");
 check((flags&(O_CREAT|O_EXCL|O_WRONLY))==(O_CREAT|O_EXCL|O_WRONLY),"exclusive open flags");
 failFd=true;check(nativeOpen("/sd/a",OpenMode::Exclusive)==nullptr&&closed==1,"open failure has no descriptor cleanup");
}
}
namespace board {
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
constexpr bool LORA_SHARED_WITH_SD=true;
#else
constexpr bool LORA_SHARED_WITH_SD=false;
#endif
}
namespace tunnel {
struct Observation {uint32_t session;};
struct Counters {std::atomic<uint32_t> storage_errors{0},log_drops{0},flush_max_ms{0};} counters;
constexpr int portMAX_DELAY=0;int busMutex=1;
unsigned yieldCalls=0;uint32_t tick=0;
uint32_t millis(){return ++tick;}
void xSemaphoreTake(int,int){check(!fake::busHeld,"recursive bus lock");fake::busHeld=true;}
void xSemaphoreGive(int){check(fake::busHeld,"unlock without lock");fake::busHeld=false;}
void vTaskDelay(int){check(!fake::busHeld,"yield while holding SPI lock");++yieldCalls;}
std::atomic<bool> mounted{true},openSession{false};storage::CheckedFile sessionFile;
uint32_t sequence=0,fileSession=0,bufferedRecords=0;char buffer[8192];size_t used=0;
// LOGGER_IO_IMPLEMENTATION
void reset(){fake::busHeld=true;sessionFile.close();fake::busHeld=false;fake::reset();fake::requireLock=true;mounted=true;openSession=false;used=bufferedRecords=0;counters.storage_errors=0;counters.log_drops=0;counters.flush_max_ms=0;yieldCalls=tick=0;}
void start(){Observation e{0x12345678};beginSession(e);check(sessionFile&&mounted&&openSession,"session starts");check(fake::bufferMode==_IONBF&&fake::bufferBytes==0,"logger has no stdio buffer allocation");check(fake::opens==1&&fake::statCalls==0,"exclusive creation has one handle and no exists-open");}
void batch(){std::memset(buffer,'x',1200);used=1200;bufferedRecords=3;}
void run(){
 reset();start();batch();flush();check(!counters.storage_errors&&!counters.log_drops&&used==0&&bufferedRecords==0,"batch success accounting");
 check(fake::largestWrite==(board::LORA_SHARED_WITH_SD?512:1200),"target write chunk unchanged");check(yieldCalls==(board::LORA_SHARED_WITH_SD?3:0),"shared SPI yield count");
 endSession();check(!sessionFile&&!openSession&&mounted&&fake::active==0,"stop closes and idles");
 for(int failure=0;failure<4;++failure){reset();start();batch();fake::shortWrite=failure==0;fake::fullWriteError=failure==1;fake::failFlush=failure==2;fake::failSync=failure==3;flush();check(counters.storage_errors==1&&counters.log_drops==3,"failed batch conservatively drops all pending records once");check(!mounted&&!sessionFile&&used==0&&bufferedRecords==0&&fake::active==0,"failed batch closes and marks storage unavailable");endSession();check(!openSession&&counters.storage_errors==1,"stop after fault does not duplicate fault");}
 reset();start();batch();flush();fake::failClose=true;endSession();check(counters.storage_errors==1&&!mounted&&!openSession&&!sessionFile&&fake::active==0,"close-only failure cannot report healthy saved storage");check(counters.log_drops==0,"successful sync is not fabricated record loss");
 reset();start();fake::failSync=true;endSession();check(counters.storage_errors==1&&!mounted&&fake::active==0,"empty final close checks sync");
 reset();fake::nodes["/sd/sessions/LT-12345678.jsonl"].bytes="old";Observation event{0x12345678};beginSession(event);check(!sessionFile&&!mounted&&counters.storage_errors==1,"collision faults");check(fake::nodes.begin()->second.bytes=="old","collision cannot truncate existing session");endSession();
 reset();fake::failOpen=true;beginSession(event);check(!sessionFile&&!mounted&&counters.storage_errors==1&&fake::active==0,"open failure is visible without leak");endSession();
 reset();start();batch();fake::failClose=true;Observation next{0x12345679};beginSession(next);check(!mounted&&!sessionFile&&counters.storage_errors==1&&fake::opens==1,"failed previous close prevents new session file");endSession();reset();fake::requireLock=false;
}
}
void helperTests(){
 using namespace tunnel::storage;
 fake::reset();check(status("/missing")==PathStatus::Missing,"ENOENT is missing");check(fake::opens==0,"stat does not consume file handle");fake::failStat=true;check(status("/denied")==PathStatus::Error,"EACCES is not missing");fake::failStat=false;
 for(const char* path:{"relative","/","/../x","/a/./b","/a//b","/a\\b"})check(status(path)==PathStatus::Error,"invalid mapped path rejected");
 check(tunnel::storage::mkdir("/config")&&tunnel::storage::mkdir("/config"),"mkdir existing directory succeeds");check(status("/config")==PathStatus::Directory,"directory metadata");
 {CheckedFile f;char buf[512];check(f.open("/config/temp",OpenMode::Truncate,buf,sizeof(buf)),"buffered open");check(fake::bufferAddress==buf&&fake::bufferBytes==512&&fake::bufferMode==_IOFBF,"uses exact caller buffer");check(f.write(reinterpret_cast<const uint8_t*>("abc"),3)==3&&f.flush()&&f.close(),"all write stages succeed");check(f.close()&&fake::closes==1,"idempotent close");}
 check(tunnel::storage::rename("/config/temp","/config/value"),"rename success");
 {CheckedFile f;char buf[512];check(f.open("/config/value",OpenMode::Read,buf,sizeof(buf))&&f.size()==3,"read size");check(f.peek()=='a'&&f.peek()=='a'&&f.read()=='a',"peek preserves byte");char rest[4]{};check(f.readBytes(rest,4)==2&&std::string(rest)=="bc","bounded short EOF read");check(f.read()==-1&&!f.failed()&&f.close(),"EOF is not error");}
 check(tunnel::storage::remove("/config/value")&&status("/config/value")==PathStatus::Missing,"remove success");
 for(int failure=0;failure<6;++failure){fake::reset();CheckedFile f;char buf[512];check(f.open("/data",OpenMode::Truncate,buf,sizeof(buf)),"fault-test open");fake::shortWrite=failure==0;fake::fullWriteError=failure==1;fake::failFlush=failure==2;fake::failSync=failure==3;fake::failClose=failure==4;fake::failDescriptor=failure==5;const size_t written=f.write(reinterpret_cast<const uint8_t*>("abc"),3);if(failure<2)check(f.failed()&&(written<3||failure==1),"write errors latched");else check(written==3,"late failure accepted full write first");check(!f.close()&&!f&&fake::active==0&&fake::closes==1,"late failure closes once and remains failed");const Failure expected=failure<2?Failure::Write:failure==2?Failure::Flush:failure==4?Failure::Close:Failure::Sync;check(f.failure()==expected,"failure phase preserved");}
 fake::reset();{CheckedFile f;check(f.open("/bad",OpenMode::Truncate),"sticky open");fake::shortWrite=true;f.write(reinterpret_cast<const uint8_t*>("abc"),3);fake::failClose=true;check(!f.close()&&f.failure()==Failure::Write&&f.error()==ENOSPC,"close failure preserves initial cause");fake::shortWrite=fake::failClose=false;check(f.open("/good",OpenMode::Truncate)&&!f.failed()&&f.close(),"new open resets sticky error");}
 fake::reset();{CheckedFile f;fake::failBuffer=true;char b[512];check(!f.open("/bad",OpenMode::Truncate,b,sizeof(b))&&!f&&f.failure()==Failure::Buffer&&fake::active==0,"buffer failure cleans handle");}
 fake::reset();{CheckedFile f;check(f.open("/held",OpenMode::Truncate),"open held");check(!f.open("/second",OpenMode::Truncate)&&fake::active==1&&fake::opens==1,"open while open cannot leak or replace");check(!f.close()&&fake::active==0,"close after misuse releases original");}
 fake::reset();fake::nodes["/sd/read"].bytes="x";{CheckedFile f;check(f.open("/read",OpenMode::Read),"read fault open");fake::failRead=true;check(f.read()==-1&&f.failed()&&!f.close()&&f.failure()==Failure::Read,"read error rejects even at EOF-shaped result");}
 fake::reset();{CheckedFile first,second,third;check(first.open("/one",OpenMode::Truncate)&&second.open("/two",OpenMode::Truncate),"two VFS slots available");check(!third.open("/three",OpenMode::Truncate)&&fake::peak==2,"regular handle cap preserved");check(first.close()&&second.close(),"slots released");}
 fake::reset();nativeOpenCheck::run();
}
int main(){try{setOperationsForTest(&fake::operations);helperTests();tunnel::run();setOperationsForTest(nullptr);std::cout<<"Checked SD/helper and actual logger I/O: "<<checks<<" checks passed ("<<(board::LORA_SHARED_WITH_SD?"Cardputer":"Heltec")<<").\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
