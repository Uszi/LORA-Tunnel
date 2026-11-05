#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "app_types.h"
using namespace tunnel;
namespace tunnel { QueueHandle_t captureQueue=nullptr; }
namespace {
constexpr unsigned capacity=12;
struct BoundedQueue {
  uint32_t before=0x7a129c45;
  std::array<unsigned char,capacity*sizeof(CaptureObservation)> storage{};
  uint32_t after=0x235db897;
  unsigned head=0,count=0;
} queue;
unsigned checks=0,consumed=0;
void expect(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
constexpr int pdTRUE=1;
int xQueueSend(QueueHandle_t handle,const CaptureObservation* event,int){
  auto& q=*static_cast<BoundedQueue*>(handle);
  if(q.count==capacity)return 0;
  const auto next=(q.head+q.count)%capacity;
  std::memcpy(q.storage.data()+next*sizeof(CaptureObservation),event,sizeof(CaptureObservation));++q.count;return pdTRUE;
}
int xQueueReceive(QueueHandle_t handle,void* event,int){
  auto& q=*static_cast<BoundedQueue*>(handle);if(!q.count)return 0;
  std::memcpy(event,q.storage.data()+q.head*sizeof(CaptureObservation),sizeof(CaptureObservation));q.head=(q.head+1)%capacity;--q.count;return pdTRUE;
}
void consume(Observation& event){
  expect(event.session==consumed+1,"ordered session/sequence survives queue copy");
  expect(event.length==RAW_MAX && event.original_length==RAW_MAX,"full raw bounds survive");
  bool raw=true;for(unsigned i=0;i<RAW_MAX;++i)raw&=event.raw[i]==static_cast<uint8_t>(i+event.session);
  expect(raw,"every raw byte survives queue copy");
  expect(event.gps.valid && event.gps.lat==12.25 && event.gps.lon==-45.5,"GPS fields survive");
  expect(event.ssid_length==32 && event.ssid_raw[31]==0xfe,"SSID bytes survive");
  expect(event.radio.frequency_mhz==(event.session%2?906.875f:910.525f) &&
      event.radio.bandwidth_khz==(event.session%2?250.0f:62.5f) &&
      event.radio.protocol==(event.session%2?1:2) &&
      event.radio.sync_word==(event.session%2?0x2b:0x12),"captured RF metadata survives profile changes");
  expect(!std::strcmp(event.radio.name,event.session%2?"US LongFast":"US-NARROW"),"captured profile label survives later producer mutations");
  expect(event.detection.category==recon::Category::None && !event.detection.remote_id.decoded && !event.detection.beacon.valid && event.detection.evidence[0]==0,"classification is reset before each actual consume");
  if(event.kind==Kind::Lora) {
    new (&event.mesh_data) mesh::Result{};
    event.mesh_data.protocol=mesh::Protocol::MeshCore;event.mesh_data.status=mesh::Status::Decoded;
    event.mesh_data.has_node=event.mesh_data.has_position=true;
    event.mesh_data.latitude_i=-900000000;event.mesh_data.longitude_i=1800000000;
    std::memset(event.mesh_data.public_key,0xad,32);std::strcpy(event.mesh_data.name,"synthetic mesh observation");
    const Observation writer(event);
    expect(writer.mesh_data.public_key[31]==0xad && writer.mesh_data.has_position &&
        writer.mesh_data.latitude_i==-900000000 && writer.mesh_data.longitude_i==1800000000 &&
        !std::strcmp(writer.mesh_data.name,"synthetic mesh observation"),"complete mesh evidence survives writer record copy");
    expect(writer.raw[RAW_MAX-1]==event.raw[RAW_MAX-1] && writer.radio.frequency_mhz==event.radio.frequency_mhz,
        "writer retains the raw packet and its capture profile with decoded evidence");
  } else {
    event.detection.category=recon::Category::Drone;event.detection.remote_id.decoded=true;event.detection.beacon.valid=true;std::strcpy(event.detection.evidence,"previous record must not leak");
  }
  ++consumed;
}
#include "drain_actual.inc"
void enqueue(unsigned sequence){
  CaptureObservation event{};event.session=sequence;event.mode=sequence%2?CaptureMode::Recon:CaptureMode::Wardriver;
  event.kind=sequence%3==0?Kind::Lora:sequence%3==1?Kind::Wifi:Kind::Ble;event.length=event.original_length=RAW_MAX;
  if(event.kind==Kind::Lora)event.mode=CaptureMode::Wardriver;
  event.gps.valid=true;event.gps.lat=12.25;event.gps.lon=-45.5;event.ssid_length=32;event.ssid_raw[31]=0xfe;
  for(unsigned i=0;i<RAW_MAX;++i)event.raw[i]=static_cast<uint8_t>(i+sequence);
  event.radio.frequency_mhz=sequence%2?906.875f:910.525f;
  event.radio.bandwidth_khz=sequence%2?250.0f:62.5f;
  event.radio.protocol=sequence%2?1:2;event.radio.sync_word=sequence%2?0x2b:0x12;
  std::strcpy(event.radio.name,sequence%2?"US LongFast":"US-NARROW");
  expect(xQueueSend(captureQueue,&event,0)==pdTRUE,"all twelve advertised ingress slots remain usable");
  event.radio=lora_scan::Snapshot{}; // Changing the producer cannot relabel queued frames.
}
}
int main(){
 captureQueue=&queue;
 expect(sizeof(lora_scan::Snapshot)==48,"each capture retains a bounded 48-byte radio snapshot");
 expect(sizeof(CaptureObservation)==1328,"ingress size is exactly 1328 bytes including RF snapshot");
 expect(sizeof(Observation)==1648,"complete writer evidence fits 1648 bytes with the shared result union");
 expect(sizeof(mesh::Result)<=sizeof(recon::Result),"mesh decoding does not enlarge the classified writer payload");
 expect((sizeof(Observation)-sizeof(CaptureObservation))*(12+2)==4480,"queue plus scratch saving is 4480 bytes");
 for(unsigned i=1;i<=12;++i)enqueue(i);
 CaptureObservation overflow{};expect(xQueueSend(captureQueue,&overflow,0)==0 && queue.count==12,"full queue refuses extra records without overwrite");
 drain(5);expect(consumed==5 && queue.count==7,"actual drain respects its bounded work budget");
 for(unsigned i=13;i<=17;++i)enqueue(i);
 drain();expect(consumed==17 && queue.count==0,"wrapped queue drains in order");
 drain();expect(consumed==17,"empty queue does not consume stale data");
 expect(queue.before==0x7a129c45 && queue.after==0x235db897,"queue sentinels remain intact");
 std::printf("%u capture queue checks passed; ingress=%zu writer=%zu saving=4480 bytes; 12 ingress slots preserved\n",checks,sizeof(CaptureObservation),sizeof(Observation));
}
