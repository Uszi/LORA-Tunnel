#pragma once
#include <Arduino.h>
namespace host {extern std::map<std::string,std::string> preferences;extern bool failPreferences;}
class Preferences {
  std::string space_;
public:
  bool begin(const char* name,bool){space_=name;return !host::failPreferences;}
  size_t getString(const char* key,char* destination,size_t capacity){auto found=host::preferences.find(space_+":"+key);if(found==host::preferences.end())return 0;strlcpy(destination,found->second.c_str(),capacity);return found->second.size();}
  size_t putString(const char* key,const char* value){if(host::failPreferences)return 0;host::preferences[space_+":"+key]=value;return std::strlen(value);}
  void end(){}
};
