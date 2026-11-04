#include "checked_sd_file.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(_WIN32)
#include <io.h>
#include <direct.h>
#endif

namespace tunnel { namespace storage {
namespace {
constexpr size_t PATH_BYTES=160;
bool mountedPath(const char* path,char (&full)[PATH_BYTES]) {
  if(!path || path[0]!='/' || !path[1]) { errno=EINVAL; return false; }
  for(const char* p=path;*p;++p) {
    if(*p=='\\' || (*p=='/' && p[1]=='/') ||
       (*p=='/' && p[1]=='.' && (p[2]=='/' || p[2]=='\0' ||
        (p[2]=='.' && (p[3]=='/' || p[3]=='\0'))))) { errno=EINVAL; return false; }
  }
  const size_t length=std::strlen(path);
  if(length+4>sizeof(full)) { errno=ENAMETOOLONG; return false; }
  std::memcpy(full,"/sd",3);std::memcpy(full+3,path,length+1);return true;
}
FILE* nativeOpen(const char* path,OpenMode mode) {
  if(mode!=OpenMode::Exclusive) return std::fopen(path,mode==OpenMode::Read?"rb":"wb");
  const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0666);
  if(fd<0) return nullptr;
  FILE* file=::fdopen(fd,"wb");
  if(!file) { const int saved=errno;::close(fd);errno=saved; }
  return file;
}
int nativeSync(int fd) {
#if defined(_WIN32)
  return ::_commit(fd);
#else
  return ::fsync(fd);
#endif
}
int nativeMkdir(const char* path) {
#if defined(_WIN32)
  return ::_mkdir(path);
#else
  return ::mkdir(path,0777);
#endif
}
#if defined(LORA_TUNNEL_STORAGE_TEST)
const Operations nativeOperations={nativeOpen,std::setvbuf,std::fread,std::fwrite,std::ferror,
  std::fflush,::fileno,nativeSync,std::fclose,::fstat,::stat,std::remove,std::rename,nativeMkdir};
const Operations* operations=&nativeOperations;
#define SD_CALL(name,native,...) operations->name(__VA_ARGS__)
#else
#define SD_CALL(name,native,...) native(__VA_ARGS__)
#endif
}
#if defined(LORA_TUNNEL_STORAGE_TEST)
void setOperationsForTest(const Operations* replacement) { operations=replacement?replacement:&nativeOperations; }
#endif
PathStatus status(const char* path) {
  char full[PATH_BYTES];if(!mountedPath(path,full)) return PathStatus::Error;
  struct stat info{};errno=0;
  if(SD_CALL(pathStat,::stat,full,&info)!=0) return errno==ENOENT?PathStatus::Missing:PathStatus::Error;
  if(S_ISREG(info.st_mode)) return PathStatus::Regular;
  if(S_ISDIR(info.st_mode)) return PathStatus::Directory;
  errno=EINVAL;return PathStatus::Error;
}
bool remove(const char* path) { char full[PATH_BYTES];return mountedPath(path,full) && SD_CALL(remove,std::remove,full)==0; }
bool rename(const char* from,const char* to) {
  char first[PATH_BYTES],second[PATH_BYTES];
  return mountedPath(from,first) && mountedPath(to,second) && SD_CALL(rename,std::rename,first,second)==0;
}
bool mkdir(const char* path) {
  char full[PATH_BYTES];if(!mountedPath(path,full)) return false;
  if(SD_CALL(mkdir,nativeMkdir,full)==0) return true;
  return errno==EEXIST && status(path)==PathStatus::Directory;
}
void CheckedFile::fail(Failure phase,int error) {
  if(failed()) return;
  failure_=phase;error_=error?error:EIO;
}
CheckedFile::~CheckedFile() { close(); }
bool CheckedFile::open(const char* path,OpenMode mode,void* buffer,size_t bytes) {
  if(file_) { fail(Failure::Open,EBUSY);return false; }
  failure_=Failure::None;error_=0;peeked_=-2;writable_=mode!=OpenMode::Read;
  if((buffer==nullptr)!=(bytes==0)) { fail(Failure::Buffer,EINVAL);return false; }
  char full[PATH_BYTES];if(!mountedPath(path,full)) { fail(Failure::Path,errno);return false; }
  errno=0;file_=SD_CALL(open,nativeOpen,full,mode);
  if(!file_) { fail(Failure::Open,errno);return false; }
  errno=0;
  if(SD_CALL(setBuffer,std::setvbuf,file_,static_cast<char*>(buffer),buffer?_IOFBF:_IONBF,bytes)!=0) {
    fail(Failure::Buffer,errno);close();return false;
  }
  return true;
}
size_t CheckedFile::size() {
  if(!file_ || failed()) return 0;
  struct stat info{};errno=0;
  const int fd=SD_CALL(descriptor,::fileno,file_);
  if(fd<0 || SD_CALL(fileStat,::fstat,fd,&info)!=0 || info.st_size<0) { fail(Failure::Stat,errno);return 0; }
  return static_cast<size_t>(info.st_size);
}
size_t CheckedFile::read(uint8_t* data,size_t bytes) {
  if(!file_ || failed() || !bytes) return 0;
  if(!data) { fail(Failure::Read,EINVAL);return 0; }
  size_t copied=0;
  if(peeked_>=0) { data[copied++]=static_cast<uint8_t>(peeked_);peeked_=-2; }
  if(copied<bytes) {
    errno=0;copied+=SD_CALL(read,std::fread,data+copied,1,bytes-copied,file_);
    if(SD_CALL(error,std::ferror,file_)) fail(Failure::Read,errno);
  }
  return copied;
}
int CheckedFile::read() { uint8_t value;return read(&value,1)==1?value:-1; }
int CheckedFile::peek() { if(peeked_<0) peeked_=read();return peeked_; }
size_t CheckedFile::write(const uint8_t* data,size_t bytes) {
  if(!file_ || failed() || !bytes) return 0;
  if(!writable_ || !data) { fail(Failure::Write,EINVAL);return 0; }
  errno=0;const size_t written=SD_CALL(write,std::fwrite,data,1,bytes,file_);
  if(written!=bytes || SD_CALL(error,std::ferror,file_)) fail(Failure::Write,errno);
  return written;
}
bool CheckedFile::flush() {
  if(!file_) return !failed();
  if(!writable_) return !failed();
  errno=0;const int flushed=SD_CALL(flush,std::fflush,file_);
  if(flushed!=0 || SD_CALL(error,std::ferror,file_)) fail(Failure::Flush,errno);
  errno=0;const int fd=SD_CALL(descriptor,::fileno,file_);
  if(fd<0 || SD_CALL(sync,nativeSync,fd)!=0) fail(Failure::Sync,errno);
  return !failed();
}
bool CheckedFile::close() {
  if(!file_) return !failed();
  flush();
  if(SD_CALL(error,std::ferror,file_)) fail(writable_?Failure::Write:Failure::Read,EIO);
  FILE* closing=file_;file_=nullptr;peeked_=-2;
  errno=0;if(SD_CALL(close,std::fclose,closing)!=0) fail(Failure::Close,errno);
  return !failed();
}
#undef SD_CALL
} }
