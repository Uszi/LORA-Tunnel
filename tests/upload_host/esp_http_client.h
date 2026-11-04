#pragma once
#include <Arduino.h>
constexpr int ESP_OK=0,HTTP_METHOD_GET=0,HTTP_METHOD_POST=1;
struct esp_http_client_config_t{const char* url=nullptr;int timeout_ms=0;bool disable_auto_redirect=false;int(*crt_bundle_attach)(void*)=nullptr;int buffer_size=0,buffer_size_tx=0,method=0;};
namespace host {
struct HttpScript {int code=200;std::string body="{\"success\":true}";unsigned fragment=7;bool headersFail=false,openFail=false;};
struct HttpCall {esp_http_client_config_t config{};std::string url,body;std::map<std::string,std::string> headers;HttpScript script;size_t readAt=0;int promised=0;};
extern unsigned activeHttp;extern bool bleReady;
extern std::vector<HttpScript> scripts;extern std::vector<HttpCall> calls;extern void(*afterWrite)();
void verifyFinalizedPost(const esp_http_client_config_t* config);
}
using esp_http_client_handle_t=host::HttpCall*;
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config){
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
if(host::bleReady)throw std::runtime_error("TLS allocated before BLE release");
#endif
if(config->method==HTTP_METHOD_POST)host::verifyFinalizedPost(config);
++host::activeHttp;host::calls.emplace_back();auto& call=host::calls.back();call.config=*config;call.url=config->url;if(!host::scripts.empty()){call.script=host::scripts.front();host::scripts.erase(host::scripts.begin());}return &call;}
inline int esp_http_client_set_header(esp_http_client_handle_t c,const char* key,const char* value){c->headers[key]=value;return 0;}
inline int esp_http_client_open(esp_http_client_handle_t c,int size){c->promised=size;return c->script.openFail?-1:0;}
inline int esp_http_client_write(esp_http_client_handle_t c,const char* bytes,int n){c->body.append(bytes,n);if(host::afterWrite){auto callback=host::afterWrite;host::afterWrite=nullptr;callback();}return n;}
inline int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c){return c->script.headersFail?-1:int64_t(c->script.body.size());}
inline int esp_http_client_get_status_code(esp_http_client_handle_t c){return c->script.code;}
inline int esp_http_client_read(esp_http_client_handle_t c,char* out,int size){const size_t n=std::min(std::min(size_t(size),size_t(c->script.fragment)),c->script.body.size()-c->readAt);std::memcpy(out,c->script.body.data()+c->readAt,n);c->readAt+=n;return int(n);}
inline bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){return c->readAt==c->script.body.size();}
inline void esp_http_client_close(esp_http_client_handle_t){}
inline void esp_http_client_cleanup(esp_http_client_handle_t){--host::activeHttp;}
