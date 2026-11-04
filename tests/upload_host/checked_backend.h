#pragma once
#include <cerrno>
#include "checked_sd_file.h"
namespace host {
bool busHeld=false;
enum class WriteFault {None,ShortWrite,FullWriteError,Flush,Sync,Close};
WriteFault writeFault=WriteFault::None;
std::string faultState;
unsigned writeFaults=0,finalizedPosts=0;
struct CheckedHandle:OpenFile {
  int id;std::string pending;bool flushOK=false,syncOK=false;void* buffer=nullptr;
  CheckedHandle(const std::string& name,int number):OpenFile(false),id(number){path=name;}
};
std::map<int,CheckedHandle*> checkedHandles;int nextDescriptor=1;
std::map<std::string,std::string> finalizedFiles;
inline void lockCheck(){if(!busHeld)throw std::runtime_error("checked upload I/O outside StorageLock");}
inline CheckedHandle& handle(FILE* file){lockCheck();return *reinterpret_cast<CheckedHandle*>(file);}
inline std::string relative(const char* path){if(std::strncmp(path,"/sd/",4))throw std::runtime_error("checked upload escaped mount");return path+3;}
inline bool target(const CheckedHandle& h,const std::string& bytes){
  if(faultState=="staging")return h.path=="/lora-tunnel/uploads/staging.csv";
  return !faultState.empty()&&h.path.find("-0.")!=std::string::npos&&bytes.find("\"state\":\""+faultState+"\"")!=std::string::npos;
}
inline FILE* checkedOpen(const char* path,tunnel::storage::OpenMode mode){
  lockCheck();if(mode!=tunnel::storage::OpenMode::Truncate)throw std::runtime_error("uploader changed read/directory behavior");
  const auto name=relative(path);if(regularFiles>=2)throw std::runtime_error("checked upload exceeds two regular files");
  files[name].clear();finalizedFiles.erase(name);
  auto* h=new CheckedHandle(name,nextDescriptor++);checkedHandles[h->id]=h;return reinterpret_cast<FILE*>(h);
}
inline int checkedBuffer(FILE* file,char* buffer,int mode,size_t bytes){
  auto& h=handle(file);if(++bufferCalls==failBufferCall){errno=ENOMEM;return -1;}
  if(mode!=_IOFBF||!buffer||bytes!=512)throw std::runtime_error("upload writer must own exactly512-byte buffer");
  h.buffer=buffer;return 0;
}
inline size_t checkedRead(void*,size_t,size_t,FILE*){throw std::runtime_error("read unexpectedly changed to CheckedFile");}
inline size_t checkedWrite(const void* data,size_t unit,size_t count,FILE* file){
  auto& h=handle(file);if(unit!=1||!h.buffer)throw std::runtime_error("invalid checked upload write");
  const auto* bytes=static_cast<const char*>(data);const std::string next=h.pending+std::string(bytes,count);
  const bool fault=target(h,next)&&(writeFault==WriteFault::ShortWrite||writeFault==WriteFault::FullWriteError);
  if((!failWritePath.empty()&&h.path.find(failWritePath)!=std::string::npos)||fault){
    h.error=true;errno=ENOSPC;if(fault)++writeFaults;
    const size_t copied=fault&&writeFault==WriteFault::FullWriteError?count:count/2;
    h.pending.append(bytes,copied);return copied;
  }
  h.pending=next;return count;
}
inline int checkedError(FILE* file){return handle(file).error;}
inline int checkedFlush(FILE* file){
  auto& h=handle(file);files[h.path]=h.pending;
  if(fileHooks().afterFlush)fileHooks().afterFlush(h.path);
  if(target(h,h.pending)&&writeFault==WriteFault::Flush){++writeFaults;errno=EIO;return EOF;}
  h.flushOK=!h.error;return 0;
}
inline int checkedDescriptor(FILE* file){return handle(file).id;}
inline int checkedSync(int fd){
  lockCheck();auto& h=*checkedHandles.at(fd);
  if(target(h,h.pending)&&writeFault==WriteFault::Sync){++writeFaults;errno=EIO;return -1;}
  h.syncOK=h.flushOK&&!h.error;return 0;
}
inline int checkedClose(FILE* file){
  auto* h=&handle(file);const bool fault=target(*h,h->pending)&&writeFault==WriteFault::Close;
  if(!fault&&h->flushOK&&h->syncOK&&!h->error)finalizedFiles[h->path]=h->pending;
  checkedHandles.erase(h->id);delete h;
  if(fault){++writeFaults;errno=EIO;return EOF;}return 0;
}
inline int checkedFstat(int,struct stat*){throw std::runtime_error("buffered output size must use written byte count");}
inline int checkedStat(const char*,struct stat*){throw std::runtime_error("unexpected checked path operation");}
inline int checkedRemove(const char*){throw std::runtime_error("unexpected checked remove");}
inline int checkedRename(const char*,const char*){throw std::runtime_error("unexpected checked rename");}
inline int checkedMkdir(const char*){throw std::runtime_error("unexpected checked mkdir");}
const tunnel::storage::Operations checkedOperations={checkedOpen,checkedBuffer,checkedRead,checkedWrite,checkedError,checkedFlush,checkedDescriptor,checkedSync,checkedClose,checkedFstat,checkedStat,checkedRemove,checkedRename,checkedMkdir};
void verifyFinalizedPost(const esp_http_client_config_t* config){
  if(busHeld)throw std::runtime_error("TLS request while holding storage lock");
  const std::string csv="/lora-tunnel/uploads/staging.csv";
  if(!finalizedFiles.count(csv)||finalizedFiles.at(csv)!=files.at(csv))throw std::runtime_error("POST before checked CSV finalization");
  const int service=std::strstr(config->url,"api.wigle.net")?0:std::strstr(config->url,"wardrift.net")?1:2;
  bool sending=false;const std::string prefix="/"+std::to_string(service)+"-";
  for(const auto& file:finalizedFiles)if(file.first.find(prefix)!=std::string::npos&&file.second.find("\"state\":\"sending\"")!=std::string::npos&&files[file.first]==file.second)sending=true;
  if(!sending)throw std::runtime_error("POST before checked sending checkpoint finalization");
  ++finalizedPosts;
}
inline void resetCheckedWrites(){
  if(busHeld||!checkedHandles.empty())throw std::runtime_error("previous checked writer leaked lock or handle");
  writeFault=WriteFault::None;faultState.clear();writeFaults=finalizedPosts=0;finalizedFiles.clear();
}
}
void xSemaphoreTake(int,int){if(host::busHeld)throw std::runtime_error("recursive StorageLock");host::busHeld=true;}
void xSemaphoreGive(int){if(!host::busHeld)throw std::runtime_error("unbalanced StorageLock");host::busHeld=false;}
