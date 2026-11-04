// Actual scanner BLE transition code; no radio, device or network operations.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
unsigned checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
namespace tunnel {
std::atomic<bool> capturing{false},quietLatched{false},bleCaptureReady{true},blePausedForUpload{false};
std::atomic<const char*> initStage{"ready"};std::atomic<unsigned> bleTransitionPhase{0};
struct {std::atomic<unsigned> radio_errors{0};} counters;
bool outsideIdle=true,owner=false,createOK=true,startOK=true,deinitOK=true,initOK=true,initialized=true;
unsigned stops=0,deinits=0,inits=0,creates=0,starts=0,disarms=0,reboots=0;
bool loggerIdle(){return outsideIdle;}
bool loggerMaintenanceQuiescent(){return owner;}
struct Scan {void stop(){++stops;}} scan;Scan* bleScanner=&scan;
struct NimBLEDevice {
 static bool deinit(bool destroy){check(!destroy,"preserve scanner object on pause");++deinits;if(deinitOK)initialized=false;return deinitOK;}
 static bool isInitialized(){return initialized;}
};
constexpr int ESP_OK=0,ESP_BT_CONTROLLER_STATUS_IDLE=0,ESP_BT_CONTROLLER_STATUS_ENABLED=1;
int esp_bt_controller_get_status(){return initialized?ESP_BT_CONTROLLER_STATUS_ENABLED:ESP_BT_CONTROLLER_STATUS_IDLE;}
bool initializeBle(){++inits;initialized=initOK;bleCaptureReady=initOK;return initOK;}
using esp_timer_handle_t=void*;esp_timer_handle_t bleTransitionTimer=nullptr;
struct esp_timer_create_args_t {void(*callback)(void*)=nullptr;const char* name=nullptr;};
struct {void printf(const char*,const char*){}} Serial;
void esp_restart(){++reboots;}
int esp_timer_create(esp_timer_create_args_t* args,esp_timer_handle_t* out){++creates;check(args->callback&&args->name,"recovery timer callback configured");if(!createOK)return -1;*out=reinterpret_cast<void*>(1);return ESP_OK;}
int esp_timer_start_once(esp_timer_handle_t timer,uint64_t duration){++starts;check(timer&&duration==15000000,"independent15-second transition timeout preserved");return startOK?ESP_OK:-1;}
int esp_timer_stop(esp_timer_handle_t timer){check(timer!=nullptr,"disarm valid recovery timer");++disarms;return ESP_OK;}
#include "ble_transition_actual.inc"
void reset(){capturing=quietLatched=blePausedForUpload=false;bleCaptureReady=true;initStage="ready";bleTransitionPhase=0;counters.radio_errors=0;outsideIdle=true;owner=false;createOK=startOK=deinitOK=initOK=initialized=true;stops=deinits=inits=creates=starts=disarms=reboots=0;bleTransitionTimer=nullptr;}
void own(){outsideIdle=false;owner=true;}
}
int main(){try{using namespace tunnel;
 reset();check(!scannersPauseBleForUpload()&&!deinits&&!creates,"external idle is not authority to pause BLE");
 own();check(scannersPauseBleForUpload()&&blePausedForUpload&&!bleCaptureReady&&!initialized,"writer owner can pause while external loggerIdle is false");
 check(deinits==1&&stops==1&&disarms==1&&bleTransitionPhase==0,"pause stops scanner and disarms timeout");
 owner=false;check(!scannersRestoreBleAfterUpload()&&blePausedForUpload&&inits==0,"non-owner restore cannot consume pending restoration");
 owner=true;check(scannersRestoreBleAfterUpload()&&bleCaptureReady&&!blePausedForUpload&&initialized,"owner restores scanner");
 check(inits==1&&starts==2&&disarms==2,"both transitions use recovery timer");
 for(unsigned reason=0;reason<4;++reason){reset();own();capturing=reason==0;quietLatched=reason==1;bleCaptureReady=reason!=2;owner=reason!=3;check(!scannersPauseBleForUpload()&&!deinits&&!creates,"capture/quiet/not-ready/nonowner refuses pause");}
 reset();outsideIdle=false;check(!armBleTransition(1)&&!creates,"unowned busy writer refuses recovery transition");
 reset();own();createOK=false;check(!scannersPauseBleForUpload()&&bleCaptureReady&&counters.radio_errors==1&&!deinits,"timer creation failure keeps existing BLE");
 reset();own();startOK=false;check(!scannersPauseBleForUpload()&&bleCaptureReady&&counters.radio_errors==1&&!deinits,"timer arm failure keeps existing BLE");
 reset();own();deinitOK=false;check(!scannersPauseBleForUpload()&&!bleCaptureReady&&!blePausedForUpload&&counters.radio_errors==1,"pause failure remains fail-closed to capture");
 reset();own();check(scannersPauseBleForUpload(),"prepare restore failure");initOK=false;check(!scannersRestoreBleAfterUpload()&&!bleCaptureReady&&counters.radio_errors==1,"restore failure blocks capture and increments radio error");
 reset();own();check(scannersPauseBleForUpload(),"prepare restore timer failure");startOK=false;check(!scannersRestoreBleAfterUpload()&&!bleCaptureReady&&!inits&&counters.radio_errors==1,"restore timer failure cannot silently resume capture");
 std::cout<<"Actual BLE maintenance lifecycle: "<<checks<<" checks passed; no radio or device access.\n";
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}return 0;}
