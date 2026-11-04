#include <Arduino.h>
#include <atomic>
#include <iostream>
#include "alert_tones.h"
using namespace tunnel;
uint32_t hostMillis=1000;
enum class CaptureMode {Wardriver,Recon};
enum class Kind {Wifi,Ble,Lora,Start,Stop};
namespace ui {enum class Action{ToggleAlerts,Shutdown};}
struct Fix{unsigned sample=0;};
struct Observation {Kind kind=Kind::Start;CaptureMode mode=CaptureMode::Wardriver;unsigned session=0;Fix gps;uint64_t mono_ms=0;unsigned untouched=17;uint8_t address_type=0;char address[24]{};recon::Result detection;};
struct Summary{bool used=false;char id[24]{};};
struct Model {char notice[100]{},shutdown_error[100]{};bool recon=false;} model;
struct Config {bool buzzer=true;unsigned recon_alert_min=2;} config;
struct Counters {std::atomic<unsigned> storage_errors{0},log_drops{0};} counters;
bool shutdownHadSession=false;unsigned shutdownAt=0,shutdownErrors=0,shutdownDrops=0;
bool shutdownPending=false,quietActive=false,startAfterUpload=false,ready=true,stopping=false;
bool uploadBusy=false,bleReady=true,storage=true,loggerFree=true,cancelled=false,acceptMarker=true;
std::atomic<bool> capturing{false};std::atomic<unsigned> sessionId{0};unsigned totals[3]{},reconTotal=0;
std::atomic<CaptureMode> captureMode{CaptureMode::Wardriver};
Summary summaries[32];unsigned starts=0,logged=0,radioStarts=0,drains=0,stops=0,fixReads=0;
Observation lastMarker;
unsigned sounding=0,toneStarts=0,toneStops=0;bool audioReady=true;
namespace hw {
void alertTone(uint16_t hz,uint16_t){sounding=hz;if(hz)++toneStarts;else ++toneStops;}
bool alertAudioReady(){return audioReady;}
}
alerts::Scheduler alertPlayer(hw::alertTone);
alerts::AlertHistory alertHistory;
bool alertHistoryNotice=false;unsigned lastAlert=0;
Observation queuedAlert;
bool queueAlertOnDrain=false,drainSilent=true,stopSilent=true,endCaptureSilent=true;
void emitDetectionAlert(const Observation&,bool,uint32_t);
void endCapture(){endCaptureSilent=!alertPlayer.active()&&!sounding;capturing=false;}
void scannersStop(){stopSilent=!alertPlayer.active()&&!sounding;++stops;}
void drain(){++drains;drainSilent=drainSilent&&!alertPlayer.active()&&!sounding&&!capturing;
  if(queueAlertOnDrain)emitDetectionAlert(queuedAlert,true,hostMillis);}

bool uploaderBusy(){return uploadBusy;}
void uploaderCancel(){cancelled=true;}
bool loggerIdle(){return loggerFree;}
bool scannersCaptureReady(){return bleReady;}
bool gnssControlPending(){return false;}
bool storageReady(){return storage;}
unsigned esp_random(){return 123;}
Fix currentFix(){++fixReads;return {42};}
uint64_t esp_timer_get_time(){return 123456789000ULL;}
bool logObservation(const Observation& value){++logged;lastMarker=value;return acceptMarker;}
void startCapture(unsigned id,CaptureMode mode){++starts;sessionId=id;captureMode=mode;capturing=true;}
void scannersStart(){++radioStarts;}
#include "request_session_actual.inc"
unsigned checks=0;
void expect(bool condition,const char* message){++checks;if(!condition){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
void reset(){alertPlayer.setEnabled(false);alertHistory.reset();alertHistoryNotice=false;lastAlert=0;sounding=toneStarts=toneStops=0;audioReady=config.buzzer=true;config.recon_alert_min=2;hostMillis=1000;queueAlertOnDrain=false;drainSilent=stopSilent=endCaptureSilent=true;model.notice[0]=0;shutdownHadSession=false;for(auto& s:summaries)s=Summary{};shutdownPending=quietActive=startAfterUpload=stopping=uploadBusy=cancelled=false;ready=bleReady=storage=loggerFree=acceptMarker=true;capturing=false;model.recon=false;starts=logged=radioStarts=drains=stops=fixReads=0;captureMode=CaptureMode::Wardriver;sessionId=0;lastMarker=Observation{};}
Observation detection(unsigned identity=1) {
 Observation event;event.kind=Kind::Ble;event.mode=CaptureMode::Recon;
 std::snprintf(event.address,sizeof(event.address),"02:00:00:00:%02x:%02x",(identity>>8)&255,identity&255);
 event.detection.category=recon::Category::Flock;event.detection.confidence=recon::Confidence::High;
 return event;
}
void beginRecon(){reset();model.recon=true;requestSession();expect(capturing&&captureMode==CaptureMode::Recon,"successful Recon starts for alert fixture");}
void finishMotif(){hostMillis+=1500;serviceAlerts();}
void testAlerts(){
 beginRecon();const auto first=detection();emitDetectionAlert(first,true,hostMillis);
 expect(toneStarts==1&&alertPlayer.active()&&alertHistory.size()==1,"first eligible identity starts real motif and enters history");
 finishMotif();expect(!alertPlayer.active()&&!sounding,"real scheduler cuts motif at hard deadline after delayed loop");
 hostMillis+=30000;emitDetectionAlert(first,true,hostMillis);
 expect(toneStarts==1,"same identity does not repeat after former 30 second cooldown");
 // Exercise more identities than the 32-row display cache, overwriting every row.
 summaries[0].used=true;strlcpy(summaries[0].id,first.address,sizeof(summaries[0].id));
 for(unsigned id=2;id<=40;++id){auto event=detection(id);auto& row=summaries[(id-1)%32];row.used=true;strlcpy(row.id,event.address,sizeof(row.id));emitDetectionAlert(event,true,hostMillis);finishMotif();}
 expect(alertHistory.size()==40&&toneStarts==40&&std::none_of(std::begin(summaries),std::end(summaries),[&](const Summary& row){return !std::strcmp(row.id,first.address);}),"history retains 40 real alert admissions after first identity has left all 32 display rows");
 emitDetectionAlert(first,true,hostMillis);expect(toneStarts==40,"display-cache eviction cannot replay an already alerted identity");
 performAlertAction(ui::Action::ToggleAlerts,0);serviceAlerts();
 auto unseen=detection(41);emitDetectionAlert(unseen,true,hostMillis);
 expect(alertHistory.size()==40&&!alertPlayer.active()&&!sounding,"muted observations create no motif or queued history entry");
 performAlertAction(ui::Action::ToggleAlerts,1);serviceAlerts();
 expect(toneStarts==40&&!alertPlayer.active(),"unmute creates no backlog replay");
 emitDetectionAlert(first,true,hostMillis);expect(toneStarts==40,"unmute retains previously heard identity history");
 emitDetectionAlert(unseen,true,hostMillis);expect(toneStarts==41&&alertHistory.size()==41,"fresh observation after unmute can alert once");
 performAlertAction(ui::Action::ToggleAlerts,0);expect(!sounding&&!alertPlayer.active(),"actual mute action stops sound immediately");
 performAlertAction(ui::Action::ToggleAlerts,1);serviceAlerts();
 expect(toneStarts==41,"second unmute does not resume the interrupted motif");

 beginRecon();auto event=detection();emitDetectionAlert(event,true,hostMillis);queueAlertOnDrain=true;queuedAlert=detection(2);
 requestSession();expect(endCaptureSilent&&stopSilent&&drainSilent&&!sounding&&!alertPlayer.active(),"Stop silences before producer stop and queued drain");
 expect(toneStarts==1&&alertHistory.size()==1&&!capturing&&stopping,"queued Stop evidence cannot start a second motif");
 serviceStopMarker();expect(!stopping&&toneStarts==1,"post-Stop marker drain remains silent");
 emitDetectionAlert(queuedAlert,true,hostMillis+5000);serviceAlerts();expect(toneStarts==1,"idle observations cannot start audio");
 const auto retained=alertHistory.size();acceptMarker=false;requestSession();expect(alertHistory.size()==retained&&!capturing,"failed Recon Start marker preserves prior alert history");
 acceptMarker=true;bleReady=false;requestSession();expect(alertHistory.size()==retained&&!capturing,"failed Recon radio readiness preserves history");
 bleReady=true;selectMode(false);expect(alertHistory.size()==retained,"mode selection alone does not reset alert history");
 requestSession();expect(capturing&&captureMode==CaptureMode::Wardriver&&alertHistory.size()==retained,"successful Wardriver Start does not reset history");
 emitDetectionAlert(queuedAlert,true,hostMillis+6000);expect(toneStarts==1,"Wardriver session cannot emit Recon tones");
 requestSession();serviceStopMarker();selectMode(true);requestSession();
 expect(capturing&&alertHistory.size()==0&&lastAlert==0,"only successful new Recon Start resets alert history and rate guard");
 emitDetectionAlert(event,true,hostMillis);expect(toneStarts==2,"new explicit Recon session admits same identity once again");
 queueAlertOnDrain=true;queuedAlert=detection(3);selectMode(false);
 expect(!sounding&&!alertPlayer.active()&&!capturing&&stopping&&toneStarts==2,"mode switch cancels before closing and draining the former Recon session");
 expect(alertHistory.size()==1,"mode switch preserves heard identity until next successful Recon Start");
 serviceStopMarker();

 beginRecon();event=detection();emitDetectionAlert(event,true,hostMillis);queueAlertOnDrain=true;queuedAlert=detection(2);
 performAlertAction(ui::Action::Shutdown);
 expect(shutdownPending&&shutdownHadSession&&!capturing&&!sounding&&!alertPlayer.active()&&cancelled,"actual Shutdown cancels sound and upload before waiting for save");
 expect(endCaptureSilent&&stopSilent&&drainSilent&&toneStarts==1,"shutdown drains cannot trigger a late motif");
 emitDetectionAlert(queuedAlert,true,hostMillis+5000);serviceAlerts();expect(toneStarts==1,"shutdown-pending observations remain silent");
 serviceStopMarker();

 // Check both entry and service guards independently, including cancellation of an active note.
 for(unsigned gate=0;gate<7;++gate){beginRecon();event=detection();emitDetectionAlert(event,true,hostMillis);
  switch(gate){case 0:capturing=false;break;case 1:captureMode=CaptureMode::Wardriver;break;case 2:model.recon=false;break;case 3:config.buzzer=false;break;case 4:audioReady=false;break;case 5:shutdownPending=true;break;case 6:quietActive=true;break;}
  serviceAlerts();expect(!sounding&&!alertPlayer.active(),"runtime gate immediately cancels active motif");
  emitDetectionAlert(detection(2),true,hostMillis+2000);expect(toneStarts==1&&alertHistory.size()==1,"runtime gate rejects new evidence without history admission");
 }
 beginRecon();event=detection();event.mode=CaptureMode::Wardriver;emitDetectionAlert(event,true,hostMillis);
 event=detection();emitDetectionAlert(event,false,hostMillis);event.detection.confidence=recon::Confidence::Low;emitDetectionAlert(event,true,hostMillis);
 expect(toneStarts==0&&alertHistory.size()==0,"wrong event mode, disabled rule and below-threshold confidence cannot alert");
 event=detection();event.detection.category=recon::Category::None;emitDetectionAlert(event,true,hostMillis);
 expect(toneStarts==0&&alertHistory.size()==0,"unmapped category never enters history without a real motif");
 event=detection();strlcpy(event.address,"invalid",sizeof(event.address));emitDetectionAlert(event,true,hostMillis);
 expect(toneStarts==0&&alertHistory.size()==0,"invalid identity cannot enter bounded alert history");
 event=detection();emitDetectionAlert(event,true,hostMillis);emitDetectionAlert(detection(2),true,hostMillis+500);
 expect(toneStarts==1&&alertHistory.size()==1,"rate-suppressed new identity is not marked heard or queued");
 finishMotif();emitDetectionAlert(detection(2),true,hostMillis);
 expect(toneStarts==2&&alertHistory.size()==2,"later fresh observation admits a previously rate-suppressed identity");

 beginRecon();for(unsigned id=1;id<=alerts::AlertHistory::CAPACITY;++id){event=detection(id);alertHistory.remember(uint8_t(event.kind),event.address_type,event.address);}
 emitDetectionAlert(detection(129),true,hostMillis);
 expect(toneStarts==0&&alertHistory.full()&&alertHistoryNotice&&std::strstr(model.notice,"history full"),"full bounded history suppresses new sound and reports its limit");
 strlcpy(model.notice,"keep later notice",sizeof(model.notice));emitDetectionAlert(detection(130),true,hostMillis+2000);
 expect(!std::strcmp(model.notice,"keep later notice")&&alertHistory.size()==128,"full-history notice emits only once without eviction");
}
int main(){
 reset();bleReady=false;requestSession();expect(logged==0&&starts==0&&!capturing,"failed BLE restoration cannot open a session");
 reset();uploadBusy=true;requestSession();expect(cancelled&&startAfterUpload&&logged==0,"Start cancels upload and defers opening");
 uploadBusy=false;bleReady=false;startAfterUpload=false;requestSession();expect(logged==0&&starts==0,"deferred Start remains closed after restore failure");
 bleReady=true;requestSession();expect(logged==1&&starts==1&&radioStarts==1&&capturing,"restored BLE permits exactly one complete Start");
 expect(lastMarker.kind==Kind::Start&&lastMarker.mode==CaptureMode::Wardriver&&lastMarker.session==123&&lastMarker.gps.sample==42&&lastMarker.mono_ms==123456789&&lastMarker.untouched==17&&fixReads==1,"actual marker helper preserves Start fields, timing, and default initialization");
 reset();stopping=true;requestSession();expect(logged==0,"saving gate precedes session creation");
 reset();storage=false;requestSession();expect(logged==0,"missing storage cannot open session");
 reset();ready=false;requestSession();expect(logged==0,"boot readiness still gates Start");
 reset();acceptMarker=false;requestSession();expect(logged==1&&starts==0&&radioStarts==0&&!capturing,"failed Start marker does not start capture or radios");
 reset();model.recon=true;requestSession();expect(lastMarker.mode==CaptureMode::Recon&&captureMode==CaptureMode::Recon,"Recon Start marker retains chosen mode");
 const unsigned startLogged=logged;requestSession();
 expect(!capturing&&stopping&&stops==1&&drains==1&&logged==startLogged,"Stop halts producers and drains before queuing its marker");
 model.recon=false;acceptMarker=false;loggerFree=false;serviceStopMarker();
 expect(stopping&&logged==startLogged+1&&drains==2,"full writer queue keeps Stop pending for retry");
 expect(lastMarker.kind==Kind::Stop&&lastMarker.mode==CaptureMode::Recon&&lastMarker.session==123&&lastMarker.gps.sample==42&&lastMarker.mono_ms==123456789&&lastMarker.untouched==17,"Stop marker retains session mode, identity, fix, timestamp and defaults");
 acceptMarker=true;serviceStopMarker();
 expect(stopping&&logged==startLogged+2&&drains==3,"accepted Stop waits for the writer to finish");
 serviceStopMarker();expect(logged==startLogged+2&&drains==3,"accepted Stop is not queued or drained twice");
 loggerFree=true;serviceStopMarker();expect(!stopping&&logged==startLogged+2,"writer completion closes the pending session once");
 testAlerts();
 std::cout<<"Actual session control, markers and alert lifecycle: "<<checks<<" checks passed; no device access.\n";
}
