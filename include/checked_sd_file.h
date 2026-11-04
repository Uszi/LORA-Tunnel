#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>

namespace tunnel { namespace storage {
enum class OpenMode { Read, Truncate, Exclusive };
enum class PathStatus { Missing, Regular, Directory, Error };
enum class Failure { None, Path, Open, Buffer, Read, Write, Flush, Sync, Close, Stat };

// All operations require the caller's existing busMutex. Paths are SD-relative
// absolute paths ("/sessions/..."), mapped to the already mounted "/sd" VFS.
// The same mount's regular-file limit applies to stdio and Arduino File alike.
PathStatus status(const char* path);
bool remove(const char* path);
bool rename(const char* from,const char* to);
bool mkdir(const char* path);

class CheckedFile {
public:
  CheckedFile()=default;
  ~CheckedFile();
  CheckedFile(const CheckedFile&)=delete;
  CheckedFile& operator=(const CheckedFile&)=delete;
  // Buffer belongs to the caller and must remain alive until close(). A null
  // buffer with size zero selects unbuffered I/O; no hidden 4 KiB buffer.
  bool open(const char* path,OpenMode mode,void* buffer=nullptr,size_t bytes=0);
  explicit operator bool() const { return file_!=nullptr; }
  size_t size();
  int read();
  int peek();
  size_t read(uint8_t* data,size_t bytes);
  size_t readBytes(char* data,size_t bytes) { return read(reinterpret_cast<uint8_t*>(data),bytes); }
  size_t write(uint8_t value) { return write(&value,1); }
  size_t write(const uint8_t* data,size_t bytes);
  // Explicit close is required before claiming success. Both methods retain
  // the first failure; close still releases the handle after a prior failure.
  bool flush();
  bool close();
  bool failed() const { return failure_!=Failure::None; }
  Failure failure() const { return failure_; }
  int error() const { return error_; }
private:
  void fail(Failure phase,int error);
  FILE* file_=nullptr;
  bool writable_=false;
  int peeked_=-2;
  Failure failure_=Failure::None;
  int error_=0;
};

#if defined(LORA_TUNNEL_STORAGE_TEST)
// Fault injection only in host builds; production calls the C/POSIX functions
// directly, without a runtime dispatch table or test API.
struct Operations {
  FILE* (*open)(const char*,OpenMode);
  int (*setBuffer)(FILE*,char*,int,size_t);
  size_t (*read)(void*,size_t,size_t,FILE*);
  size_t (*write)(const void*,size_t,size_t,FILE*);
  int (*error)(FILE*);
  int (*flush)(FILE*);
  int (*descriptor)(FILE*);
  int (*sync)(int);
  int (*close)(FILE*);
  int (*fileStat)(int,struct stat*);
  int (*pathStat)(const char*,struct stat*);
  int (*remove)(const char*);
  int (*rename)(const char*,const char*);
  int (*mkdir)(const char*);
};
// Every live CheckedFile must be closed before changing this test backend.
void setOperationsForTest(const Operations* operations);
#endif
} }
