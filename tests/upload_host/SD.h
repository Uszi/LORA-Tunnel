#pragma once
#include <Arduino.h>
constexpr const char* FILE_READ="r";constexpr const char* FILE_WRITE="w";
namespace host {
extern std::map<std::string,std::string> files;
extern std::vector<std::string> directories;
extern std::string failWritePath;
struct OpenFile {std::string path;size_t pos=0;bool directory=false,error=false;std::vector<std::string> entries;size_t entry=0;};
}
class File {
  std::shared_ptr<host::OpenFile> handle_;
public:
  File()=default;
  explicit File(const std::string& path,bool directory=false):handle_(std::make_shared<host::OpenFile>()) {
    handle_->path=path;handle_->directory=directory;
    if(directory)for(const auto& item:host::files)if(item.first.substr(0,path.size()+1)==path+"/"&&item.first.find('/',path.size()+1)==std::string::npos)handle_->entries.push_back(item.first);
  }
  explicit operator bool() const{return bool(handle_);}
  bool isDirectory() const{return handle_&&handle_->directory;}
  const char* name() const{return handle_?handle_->path.c_str():"";}
  size_t size()const{return handle_?host::files[handle_->path].size():0;}
  int available()const{return handle_&&handle_->pos<size();}
  int read(){if(!available())return -1;return static_cast<unsigned char>(host::files[handle_->path][handle_->pos++]);}
  size_t read(uint8_t* out,size_t n){const size_t got=std::min(n,size()-handle_->pos);std::memcpy(out,host::files[handle_->path].data()+handle_->pos,got);handle_->pos+=got;return got;}
  size_t readBytes(char* out,size_t n){return read(reinterpret_cast<uint8_t*>(out),n);}
  bool seek(size_t pos){if(!handle_||pos>size())return false;handle_->pos=pos;return true;}
  size_t write(const uint8_t* bytes,size_t n){if(!handle_)return 0;if(!host::failWritePath.empty()&&handle_->path.find(host::failWritePath)!=std::string::npos){handle_->error=true;return 0;}auto& data=host::files[handle_->path];if(data.size()<handle_->pos+n)data.resize(handle_->pos+n);std::memcpy(&data[handle_->pos],bytes,n);handle_->pos+=n;return n;}
  size_t write(uint8_t byte){return write(&byte,1);}
  void flush(){}
  bool getWriteError()const{return handle_&&handle_->error;}
  void close(){handle_.reset();}
  File openNextFile(){if(!handle_||handle_->entry>=handle_->entries.size())return File{};return File(handle_->entries[handle_->entry++]);}
};
class SDClass {
public:
  bool exists(const char* path){return host::files.count(path)||std::find(host::directories.begin(),host::directories.end(),path)!=host::directories.end();}
  bool mkdir(const char* path){if(!exists(path))host::directories.push_back(path);return true;}
  bool remove(const char* path){return host::files.erase(path)>0;}
  File open(const char* path,const char* mode=FILE_READ){if(!std::strcmp(mode,FILE_WRITE)){host::files[path].clear();return File(path);}if(host::files.count(path))return File(path);if(exists(path))return File(path,true);return File{};}
};
extern SDClass SD;
