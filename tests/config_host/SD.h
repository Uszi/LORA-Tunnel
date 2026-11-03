#pragma once
#define SD HostBaseSD
#include "../upload_host/SD.h"
#undef SD
namespace host { extern bool failRename; extern std::string failOpenPath; }
class ConfigSD:public SDClass {
public:
  File open(const char* path,const char* mode=FILE_READ) {
    if(host::failOpenPath==path)return File{};
    return SDClass::open(path,mode);
  }
  bool rename(const char* from,const char* to) {
    if(host::failRename || !host::files.count(from) || host::files.count(to))return false;
    host::files[to]=host::files[from];host::files.erase(from);return true;
  }
};
extern ConfigSD SD;
