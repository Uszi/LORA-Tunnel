#include <Arduino.h>
#include <atomic>
#include <iostream>
enum class CaptureMode {Wardriver,Recon};
enum class Kind {Start,Stop};
struct Fix{unsigned sample=0;};
struct Observation {Kind kind=Kind::Start;CaptureMode mode=CaptureMode::Wardriver;unsigned session=0;Fix gps;uint64_t mono_ms=0;unsigned untouched=17;};
struct Summary{};
struct Model {char notice[100]{};bool recon=false;} model;
bool shutdownPending=false,quietActive=false,startAfterUpload=false,ready=true,stopping=false;
bool uploadBusy=false,bleReady=true,storage=true,loggerFree=true,cancelled=false,acceptMarker=true;
std::atomic<bool> capturing{false};std::atomic<unsigned> sessionId{0};unsigned totals[3]{},reconTotal=0;
std::atomic<CaptureMode> captureMode{CaptureMode::Wardriver};
Summary summaries[2];unsigned starts=0,logged=0,radioStarts=0,drains=0,stops=0,fixReads=0;
Observation lastMarker;
void endCapture(){capturing=false;}
void scannersStop(){++stops;}
void drain(){++drains;}
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
void reset(){shutdownPending=quietActive=startAfterUpload=stopping=uploadBusy=cancelled=false;ready=bleReady=storage=loggerFree=acceptMarker=true;capturing=false;model.recon=false;starts=logged=radioStarts=drains=stops=fixReads=0;captureMode=CaptureMode::Wardriver;sessionId=0;lastMarker=Observation{};}
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
 std::cout<<"Actual session control and markers: "<<checks<<" checks passed; no device access.\n";
}
