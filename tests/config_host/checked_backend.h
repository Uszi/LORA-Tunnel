#pragma once
#include <cerrno>
#include <cstddef>
#include <stdexcept>
#include "checked_sd_file.h"
namespace host {
alignas(std::max_align_t) uint8_t frameScratch[32400];
bool leaseActive=false;
size_t leaseBytes=0;
std::string failReadPath,failFlushPath,failSyncPath,failClosePath,failStatPath;
bool failMkdir=false;
struct CheckedHandle:OpenFile {
  int id;void* buffer=nullptr;
  CheckedHandle(const std::string& name,int number):OpenFile(false),id(number){path=name;}
};
std::map<int,CheckedHandle*> checkedHandles;int nextDescriptor=1;
inline std::string relative(const char* path){if(std::strncmp(path,"/sd/",4))throw std::runtime_error("checked file escaped mounted SD path");return path+3;}
inline CheckedHandle& handle(FILE* f){return *reinterpret_cast<CheckedHandle*>(f);}
inline bool fault(const std::string& expected,const std::string& path){return !expected.empty()&&expected==path;}
inline FILE* checkedOpen(const char* path,tunnel::storage::OpenMode mode){
  const auto name=relative(path);if(fault(failOpenPath,name)){errno=EIO;return nullptr;}
  if(mode==tunnel::storage::OpenMode::Read&&!files.count(name)){errno=ENOENT;return nullptr;}
  if(mode==tunnel::storage::OpenMode::Exclusive&&files.count(name)){errno=EEXIST;return nullptr;}
  if(regularFiles>=2)throw std::runtime_error("checked config exceeds two file slots");
  if(mode!=tunnel::storage::OpenMode::Read)files[name].clear();
  auto* h=new CheckedHandle(name,nextDescriptor++);checkedHandles[h->id]=h;return reinterpret_cast<FILE*>(h);
}
inline int checkedBuffer(FILE* f,char* buffer,int mode,size_t bytes){
  if(++bufferCalls==failBufferCall){errno=ENOMEM;return -1;}
  if(!leaseActive||mode!=_IOFBF||bytes!=512||reinterpret_cast<uint8_t*>(buffer)!=frameScratch+16384)
    throw std::runtime_error("checked config buffer is not the leased512-byte tail");
  handle(f).buffer=buffer;return 0;
}
inline size_t checkedRead(void* data,size_t size,size_t count,FILE* f){
  auto& h=handle(f);if(fault(failReadPath,h.path)){h.error=true;errno=EIO;return 0;}
  const auto& value=files[h.path];const size_t n=std::min(size*count,value.size()-h.pos);
  std::memcpy(data,value.data()+h.pos,n);h.pos+=n;return size?n/size:0;
}
inline size_t checkedWrite(const void* data,size_t size,size_t count,FILE* f){
  auto& h=handle(f);if(!failWritePath.empty()&&h.path.find(failWritePath)!=std::string::npos){h.error=true;errno=ENOSPC;return 0;}
  const size_t n=size*count;auto& value=files[h.path];if(value.size()<h.pos+n)value.resize(h.pos+n);
  std::memcpy(&value[h.pos],data,n);h.pos+=n;return count;
}
inline int checkedError(FILE* f){return handle(f).error;}
inline int checkedFlush(FILE* f){if(fault(failFlushPath,handle(f).path)){errno=EIO;return -1;}return 0;}
inline int checkedDescriptor(FILE* f){return handle(f).id;}
inline int checkedSync(int fd){if(fault(failSyncPath,checkedHandles.at(fd)->path)){errno=EIO;return -1;}return 0;}
inline int checkedClose(FILE* f){auto* h=&handle(f);const bool fail=fault(failClosePath,h->path);checkedHandles.erase(h->id);delete h;if(fail){errno=EIO;return -1;}return 0;}
inline int checkedFstat(int fd,struct stat* out){const auto& h=*checkedHandles.at(fd);std::memset(out,0,sizeof(*out));out->st_mode=S_IFREG;out->st_size=files[h.path].size();return 0;}
inline int checkedStat(const char* path,struct stat* out){
  const auto name=relative(path);if(fault(failStatPath,name)){errno=EIO;return -1;}
  std::memset(out,0,sizeof(*out));if(files.count(name)){out->st_mode=S_IFREG;out->st_size=files[name].size();return 0;}
  if(std::find(directories.begin(),directories.end(),name)!=directories.end()){out->st_mode=S_IFDIR;return 0;}
  errno=ENOENT;return -1;
}
inline int checkedRemove(const char* path){if(files.erase(relative(path)))return 0;errno=ENOENT;return -1;}
inline int checkedRename(const char* first,const char* second){const auto a=relative(first),b=relative(second);if(failRename||!files.count(a)||files.count(b)){errno=EIO;return -1;}files[b]=files[a];files.erase(a);return 0;}
inline int checkedMkdir(const char* path){if(failMkdir){errno=EIO;return -1;}const auto name=relative(path);if(std::find(directories.begin(),directories.end(),name)!=directories.end()){errno=EEXIST;return -1;}directories.push_back(name);return 0;}
const tunnel::storage::Operations checkedOperations={checkedOpen,checkedBuffer,checkedRead,checkedWrite,checkedError,checkedFlush,checkedDescriptor,checkedSync,checkedClose,checkedFstat,checkedStat,checkedRemove,checkedRename,checkedMkdir};
inline void resetCheckedFaults(){failReadPath.clear();failFlushPath.clear();failSyncPath.clear();failClosePath.clear();failStatPath.clear();failMkdir=false;}
}
namespace ui {
void* acquireConfigScratch(size_t bytes){
  ++host::scratchCalls;
  if(host::failScratch||host::leaseActive||bytes>sizeof(host::frameScratch))return nullptr;
  host::leaseActive=true;host::leaseBytes=bytes;host::scratchLive+=bytes;host::scratchPeak=std::max(host::scratchPeak,host::scratchLive);
  std::memset(host::frameScratch,0xa5,sizeof(host::frameScratch));return host::frameScratch;
}
void releaseConfigScratch(void* memory){
  if(!memory)return;
  if(memory!=host::frameScratch||!host::leaseActive||host::regularFiles)throw std::runtime_error("scratch released before file lifetime ended");
  for(size_t i=host::leaseBytes;i<sizeof(host::frameScratch);++i)if(host::frameScratch[i]!=0xa5)throw std::runtime_error("scratch overwrote unleased pixels");
  volatile uint8_t* data=host::frameScratch;for(size_t i=0;i<host::leaseBytes;++i)data[i]=0;
  host::scratchLive-=host::leaseBytes;host::leaseActive=false;host::leaseBytes=0;
}
}
