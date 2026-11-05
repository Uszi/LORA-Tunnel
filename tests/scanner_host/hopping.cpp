#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include "app_types.h"

// These are the pinned SX126x raw IRQ bits and PhysicalLayer bit indexes.
// Production keeps normalized masks at startReceive and raw masks on reads.
constexpr unsigned RADIOLIB_SX126X_IRQ_RX_DONE=2;
constexpr unsigned RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED=4;
constexpr unsigned RADIOLIB_SX126X_IRQ_HEADER_VALID=16;
constexpr unsigned RADIOLIB_SX126X_RX_TIMEOUT_INF=0xffffff;
constexpr unsigned RADIOLIB_IRQ_PREAMBLE_DETECTED=2;
constexpr unsigned RADIOLIB_IRQ_RX_DEFAULT_FLAGS=2|16|32|64|512;
constexpr unsigned RADIOLIB_IRQ_RX_DEFAULT_MASK=2;
constexpr int RADIOLIB_ERR_NONE=0,pdTRUE=1;
using TickType_t=unsigned;
#define pdMS_TO_TICKS(x) (x)
unsigned nowMs=0;
unsigned millis(){return nowMs;}
bool busAvailable=true,busHeld=false;
int xSemaphoreTake(SemaphoreHandle_t,unsigned){if(!busAvailable || busHeld)return 0;busHeld=true;return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t){busHeld=false;}
namespace board {constexpr bool LORA_SHARED_WITH_SD=true,LORA_USE_LDO=false;constexpr float LORA_TCXO_VOLTS=1.8f;}
namespace hw {
bool frontend=false,present=true;
void setLoRaFrontend(bool value,bool=false){frontend=value;}
bool loraFrontendPresent(){return present;}
}
struct FakeRadio {
  unsigned flags=0,armCount=0,tunes=0,standbys=0,readCount=0,irqEnable=0,irqDio=0;
  bool raceDone=false,racePreamble=false,failTune=false,failArm=false,badCrc=false;
  float frequency=906.875,bandwidth=250;
  unsigned sf=11,cr=5,sync=0x2b,preamble=16;
  std::uint8_t packet[255]{};
  std::size_t length=20;
  int standby(){++standbys;if(raceDone){flags=2;raceDone=false;}else if(racePreamble){flags=4;racePreamble=false;}return 0;}
  int begin(float f,float bw,unsigned s,unsigned c,unsigned sw,int,unsigned pre,float,bool){frequency=f;bandwidth=bw;sf=s;cr=c;sync=sw;preamble=pre;return 0;}
  int setFrequency(float value){++tunes;if(failTune)return -1;frequency=value;return 0;}
  int setBandwidth(float value){bandwidth=value;return 0;}
  int setSpreadingFactor(unsigned value){sf=value;return 0;}
  int setCodingRate(unsigned value){cr=value;return 0;}
  int setSyncWord(unsigned value){sync=value;return 0;}
  int setPreambleLength(unsigned value){preamble=value;return 0;}
  int explicitHeader(){return 0;}int setCRC(unsigned value){return value==2?0:-1;}
  int invertIQ(bool value){return value?-1:0;}int setDio2AsRfSwitch(bool value){return value?0:-1;}
  unsigned getTimeOnAir(unsigned){return 1000000;}
  unsigned getIrqFlags(){return flags;}
  int startReceive(unsigned,unsigned enable,unsigned dio){++armCount;irqEnable=enable;irqDio=dio;flags=0;return failArm?-1:0;}
  std::size_t getPacketLength(){return length;}
  int readData(std::uint8_t* output,std::size_t count){++readCount;std::memcpy(output,packet,count);flags=0;return badCrc?-1:0;}
  float getRSSI(){return -73;}float getSNR(){return 6.5;}
};
namespace tunnel {
Counters counters;
SemaphoreHandle_t busMutex=nullptr;
std::atomic<bool> capturing{true};
std::atomic<CaptureMode> captureMode{CaptureMode::Wardriver};
std::vector<CaptureObservation> received;
void submit(CaptureObservation& event){received.push_back(event);}
namespace {
FakeRadio radio;
struct TestConfig {bool lna=true;std::uint32_t lora_dwell_ms=30000;} config;
std::atomic<bool> radioIRQ{false};
bool radioOK=true,radioInitialized=true,radioListening=true,hopDeferred=false;
std::uint32_t radioListenAt=0,maximumPacketMs=4000;
std::uint8_t radioSlot=0;
constexpr std::size_t LORA_CAPTURE_MAX=255;
lora_scan::Snapshot radioProfiles[2];
lora_scan::Status radioStatus;
lora_scan::Dwell radioDwell;
enum class RadioTransition {None,Receive,Standby};
RadioTransition radioPending=RadioTransition::None;
// SCANNER_IMPLEMENTATION
}
}
unsigned checks=0;
void expect(bool value,const char* reason){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
void reset(){
  using namespace tunnel;
  nowMs=0;busAvailable=true;busHeld=false;hw::present=true;hw::frontend=true;
  radio=FakeRadio{};radioIRQ=false;radioOK=radioInitialized=radioListening=true;hopDeferred=false;
  capturing=true;captureMode=CaptureMode::Wardriver;radioPending=RadioTransition::None;
  radioListenAt=0;radioSlot=0;maximumPacketMs=4000;received.clear();
  counters.radio_errors=0;counters.malformed=0;
  radioProfiles[0]=lora_scan::Snapshot{};auto& p=radioProfiles[0];
  p.frequency_mhz=906.875;p.bandwidth_khz=250;p.sf=11;p.cr=5;p.sync_word=0x2b;p.protocol=1;p.preamble_symbols=16;
  std::strcpy(p.name,"US LongFast");
  radioProfiles[1]=lora_scan::snapshot(*lora_scan::meshCoreProfile("US"));
  radioStatus=lora_scan::Status{};radioStatus.active=p;radioStatus.combined=true;radioStatus.meshcore_available=true;
  radioDwell.reset(nowMs,30000);
  for(unsigned i=0;i<255;++i)radio.packet[i]=static_cast<std::uint8_t>(i+1);
}
int main(){using namespace tunnel;
  reset();nowMs=29999;serviceRadioHop();expect(radioSlot==0 && radio.tunes==0,"no retune before dwell expires");
  nowMs=30000;busAvailable=false;serviceRadioHop();expect(radioSlot==0 && radio.tunes==0,"SD contention defers retune");
  busAvailable=true;serviceRadioHop();expect(radioSlot==1 && radioStatus.switches==1,"due idle profile advances to MeshCore");
  expect(radio.sync==0x12 && radio.bandwidth==62.5f && radio.sf==7 && radio.preamble==32,"complete MeshCore PHY settings applied");
  expect(radioStatus.listen_ms[0]==30000 && radioStatus.listen_ms[1]==0,"retune accounts old listening interval to Meshtastic");
  expect((radio.irqEnable&4) && radio.irqDio==2,"preamble status enabled but DIO remains completion-only");
  nowMs=60000;serviceRadioHop();expect(radioSlot==0 && radioStatus.listen_ms[1]==30000,"second dwell returns to Meshtastic with separate listening time");
  expect(!busHeld,"SPI bus released after retune");

  reset();nowMs=30000;radio.flags=RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;serviceRadioHop();
  expect(radioSlot==0 && radioStatus.hop_deferrals==1,"preamble extends dwell");
  nowMs=33999;serviceRadioHop();expect(radio.tunes==0 && radioStatus.hop_deferrals==1,"in-flight extension is counted once");
  nowMs=34000;serviceRadioHop();expect(radioSlot==1 && radioStatus.hop_timeouts==1,"stuck preamble cannot pin receiver indefinitely");

  reset();nowMs=30000;radio.flags=RADIOLIB_SX126X_IRQ_RX_DONE;serviceRadioHop();
  expect(radioIRQ && radio.tunes==0,"complete FIFO takes priority even before ISR flag arrives");
  serviceLoRa();serviceRadioHop();
  expect(received.size()==1 && radioSlot==1,"completed packet logged before profile advance");
  expect(received[0].radio.protocol==1 && received[0].radio.frequency_mhz==906.875f &&
      received[0].radio.sync_word==0x2b,"packet retains old profile after retune");
  expect(received[0].raw[0]==1 && received[0].raw[19]==20 && received[0].rssi==-73 && received[0].snr==6.5f,"packet raw bytes and radio measurements preserved");
  expect(radioStatus.received[0]==1 && radioStatus.received[1]==0,"capture counted against receiving protocol");

  reset();nowMs=30000;radio.raceDone=true;serviceRadioHop();
  expect(received.size()==1 && radioSlot==1 && received[0].radio.protocol==1,"completion racing standby is read before new settings");
  expect(radio.armCount==1,"race FIFO read does not briefly rearm old profile");
  reset();nowMs=30000;radio.racePreamble=true;serviceRadioHop();
  expect(radioSlot==1 && radioStatus.hop_interruptions==1,"new preamble racing standby is explicitly counted");
  reset();nowMs=30000;radio.failTune=true;serviceRadioHop();
  expect(!radioOK && !radioListening && !hw::frontend && counters.radio_errors==1,"retune failure disables RX and records error");
  reset();nowMs=30000;radio.failArm=true;serviceRadioHop();
  expect(!radioOK && !radioListening && !hw::frontend && counters.radio_errors==1,"rearm failure cannot be reported as active listening");
  reset();nowMs=30000;radio.flags=2;radioIRQ=true;radio.badCrc=true;serviceLoRa();
  expect(received.empty() && counters.radio_errors==1 && radioListening,"CRC failure counted without accepted packet, receive rearmed");

  reset();radioStatus.combined=false;radioSlot=1;radioStatus.active=radioProfiles[1];
  nowMs=123;radio.flags=2;radioIRQ=true;serviceLoRa();
  expect(received.size()==1 && radioStatus.received[1]==1 && radioStatus.listen_ms[1]==123,"MeshCore-only counts and metadata use slot one");
  expect(received[0].radio.protocol==2 && received[0].radio.sync_word==0x12,"MeshCore-only packets use their captured protocol");
  nowMs=60000;serviceRadioHop();expect(radio.tunes==0,"locked profile never hops");
  radioPending=RadioTransition::Standby;serviceRadioTransition();
  expect(!radioListening && !hw::frontend,"stop transition ceases listening and frontend");
  expect(!busHeld,"all exercised paths release SPI ownership");
  std::printf("%u actual radio transition checks passed; synthetic driver only, no RF acceptance claim.\n",checks);
}
