#pragma once
#include <Arduino.h>
constexpr const char* FILE_READ="r";constexpr const char* FILE_WRITE="w";
namespace host {
extern unsigned openFiles,regularFiles,peakRegularFiles,bufferCalls,failBufferCall;
extern std::map<std::string,std::string> files;
extern std::vector<std::string> directories;
extern std::string failWritePath;
struct FileHooks {void(*afterFlush)(const std::string&)=nullptr;bool dynamicDirectory=false;};
inline FileHooks& fileHooks(){static FileHooks hooks;return hooks;}
struct OpenFile {
  explicit OpenFile(bool dir):directory(dir){++openFiles;if(!dir)peakRegularFiles=std::max(peakRegularFiles,++regularFiles);}
  ~OpenFile(){--openFiles;if(!directory)--regularFiles;}
  std::string path;size_t pos=0,bufferSize=4096;bool directory=false,error=false;std::vector<std::string> entries;size_t entry=0;
};
}
class File {
  std::shared_ptr<host::OpenFile> handle_;
public:
  File()=default;
  explicit File(const std::string& path,bool directory=false) {
    if(!directory&&host::regularFiles>=2)throw std::runtime_error("Uploader exceeds two regular SD file slots");
    handle_=std::make_shared<host::OpenFile>(directory);handle_->path=path;
    if(directory)for(const auto& item:host::files)if(item.first.substr(0,path.size()+1)==path+"/"&&item.first.find('/',path.size()+1)==std::string::npos)handle_->entries.push_back(item.first);
  }
  bool setBufferSize(size_t size){if(!handle_||++host::bufferCalls==host::failBufferCall)return false;handle_->bufferSize=size;return true;}
  void checkBuffer() const {
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
    if(handle_&&!handle_->directory&&handle_->bufferSize!=512)throw std::runtime_error("Cardputer file I/O lacks bounded stdio buffer");
#endif
  }
  explicit operator bool() const{return bool(handle_);}
  bool isDirectory() const{return handle_&&handle_->directory;}
  const char* name() const{return handle_?handle_->path.c_str():"";}
  size_t size()const{return handle_?host::files[handle_->path].size():0;}
  int available()const{return handle_&&handle_->pos<size();}
  int read(){checkBuffer();if(!available())return -1;return static_cast<unsigned char>(host::files[handle_->path][handle_->pos++]);}
  size_t read(uint8_t* out,size_t n){checkBuffer();const size_t got=std::min(n,size()-handle_->pos);std::memcpy(out,host::files[handle_->path].data()+handle_->pos,got);handle_->pos+=got;return got;}
  size_t readBytes(char* out,size_t n){return read(reinterpret_cast<uint8_t*>(out),n);}
  bool seek(size_t pos){if(!handle_||pos>size())return false;handle_->pos=pos;return true;}
  size_t write(const uint8_t* bytes,size_t n){checkBuffer();if(!handle_)return 0;if(!host::failWritePath.empty()&&handle_->path.find(host::failWritePath)!=std::string::npos){handle_->error=true;return 0;}auto& data=host::files[handle_->path];if(data.size()<handle_->pos+n)data.resize(handle_->pos+n);std::memcpy(&data[handle_->pos],bytes,n);handle_->pos+=n;return n;}
  size_t write(uint8_t byte){return write(&byte,1);}
  void flush(){if(handle_&&host::fileHooks().afterFlush)host::fileHooks().afterFlush(handle_->path);}
  bool getWriteError()const{return handle_&&handle_->error;}
  void close(){handle_.reset();}
  File openNextFile(){
    if(handle_&&handle_->directory&&host::fileHooks().dynamicDirectory){
      const auto& path=handle_->path;
      for(const auto& item:host::files)if(item.first.substr(0,path.size()+1)==path+"/"&&item.first.find('/',path.size()+1)==std::string::npos&&
          std::find(handle_->entries.begin(),handle_->entries.end(),item.first)==handle_->entries.end())handle_->entries.push_back(item.first);
    }
    if(!handle_||handle_->entry>=handle_->entries.size())return File{};
    return File(handle_->entries[handle_->entry++]);
  }
};
class SDClass {
public:
  bool exists(const char* path){return host::files.count(path)||std::find(host::directories.begin(),host::directories.end(),path)!=host::directories.end();}
  bool mkdir(const char* path){if(!exists(path))host::directories.push_back(path);return true;}
  bool remove(const char* path){return host::files.erase(path)>0;}
  File open(const char* path,const char* mode=FILE_READ){if(!std::strcmp(mode,FILE_WRITE)){host::files[path].clear();return File(path);}if(host::files.count(path))return File(path);if(exists(path))return File(path,true);return File{};}
};
extern SDClass SD;
