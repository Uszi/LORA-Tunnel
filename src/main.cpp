#include <Arduino.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "app_types.h"
#include "board_config.h"
#include "hardware.h"
#include "config.h"
#include "scanners.h"
#include "logger.h"
#include "mesh_header.h"
#include "ui.h"
#include "version.h"
#include "shutdown_guard.h"
#include "resource_memory.h"
#include "uploader.h"
#include "alert_tones.h"

using namespace tunnel;
namespace {
ui::Model model;
DRAM_ATTR std::atomic<uint32_t> heapAllocationFailureSeen{0};
bool allocationHookReady=false;
void IRAM_ATTR allocationFailed(size_t,uint32_t,const char*) {
  // A latch needs only one aligned store, avoiding Xtensa atomic RMW helpers.
  heapAllocationFailureSeen.store(1,std::memory_order_relaxed);
}
bool ready=false,stopping=false,hardwareReady=false;
bool startAfterUpload=false;
bool initWriter=false,initScanner=false,initMemory=false;
uint32_t heapAfterHardware=0,heapAfterUi=0,heapAfterConfig=0,heapBeforeScanners=0;
bool shutdownPending=false,shutdownHadSession=false;
bool configScratchStartup=false;
uint32_t shutdownAt=0,shutdownErrors=0,shutdownDrops=0;
esp_timer_handle_t shutdownTimer=nullptr;
bool quietActive=false,quietRadios=false,quietDisplay=false;
esp_timer_handle_t quietTimer=nullptr;
uint32_t totals[3]{},lastAlert=0,reconTotal=0;
alerts::Scheduler alertPlayer(hw::alertTone);
alerts::AlertHistory alertHistory;
bool alertHistoryNotice=false;
StaticQueue_t captureQueueControl;
uint8_t* captureQueueStorage=nullptr;
struct Action { ui::Action action; int field; char value[128]{}; };
QueueHandle_t actions=nullptr;
struct Summary {
  bool used=false;
  Kind kind=Kind::Wifi;
  uint8_t addressType=255;
  char id[24]{},name[40]{},label[32]{},evidence[board::SUMMARY_EVIDENCE_BYTES]{};
  recon::Category category=recon::Category::None;
  recon::Confidence confidence=recon::Confidence::None;
  int rssi=0;
  uint32_t seen=0;
  uint64_t last=0;
};
Summary summaries[board::SUMMARY_RECORDS];

bool configScratchAllowed() {
  // Configuration shares display scratch only while this main task owns an
  // idle device. Startup is a distinct phase before writer/scanner tasks exist.
  return !shutdownPending && !quietActive && !capturing.load() && !stopping &&
      !uploaderBusy() && (configScratchStartup || loggerIdle());
}

void callback(ui::Action action,int field,const char* value) {
  Action request{}; request.action=action; request.field=field;
  strlcpy(request.value,value,sizeof(request.value));
  if(xQueueSend(actions,&request,0)!=pdTRUE) strlcpy(model.notice,"Busy: please try again",sizeof(model.notice));
  memset(request.value,0,sizeof(request.value));
}
void emitDetectionAlert(const Observation& event,bool allowed,uint32_t now) {
  // Drain/Stop can still consume queued evidence; that must never start audio.
  if(!capturing.load() || captureMode.load()!=CaptureMode::Recon || !model.recon ||
     shutdownPending || quietActive || !config.buzzer || !hw::alertAudioReady() ||
     event.mode!=CaptureMode::Recon || !allowed ||
     unsigned(event.detection.confidence)<config.recon_alert_min)return;
  const auto seen=alertHistory.check(uint8_t(event.kind),event.address_type,event.address);
  if(seen==alerts::HistoryStatus::Full && !alertHistoryNotice) {
    strlcpy(model.notice,"Alert history full; capture continues",sizeof(model.notice));
    alertHistoryNotice=true;
  }
  if(seen!=alerts::HistoryStatus::Unseen || (lastAlert && now-lastAlert<1000))return;
  if(alertPlayer.request(event.detection,now)) {
    alertHistory.remember(uint8_t(event.kind),event.address_type,event.address);
    lastAlert=now;
  }
}
void serviceAlerts() {
  alertPlayer.setEnabled(capturing.load() && captureMode.load()==CaptureMode::Recon &&
      model.recon && config.buzzer && hw::alertAudioReady() && !shutdownPending && !quietActive);
  alertPlayer.tick(millis());
}
void consume(Observation& event) {
  if(event.session!=sessionId.load()) return;
  const unsigned type=unsigned(event.kind);
  if(type>2) return;
  ++totals[type];
  bool alertAllowed=true;
  if(event.mode==CaptureMode::Recon) {
    const bool complete=!event.truncated;
    if(event.kind==Kind::Wifi) {
      event.detection=recon::classifyWifi(event.address,complete?event.name:"",
          complete && event.wifi_frame?event.raw:nullptr,complete && event.wifi_frame?event.length:0);
      if(!complete) event.detection.payload_valid=false; // Address-only evidence.
    } else if(event.kind==Kind::Ble) {
      if(complete) event.detection=recon::classifyBle(event.address,event.address_type,event.raw,event.length,event.ble_adv_length);
    }
    else return;
    // Probe requests name a desired network, not their transmitter. Only a
    // complete validated beacon/probe response supplies a Wi-Fi name rule.
    const bool transmitterName=event.kind==Kind::Ble || !event.wifi_frame ||
        (event.length>=36 && (event.raw[0]==0x80 || event.raw[0]==0x50));
    const bool payloadValid=complete && event.detection.payload_valid;
    const bool blePayload=event.kind==Kind::Ble && payloadValid;
    const auto rule=applyReconRules(event.detection,event.kind==Kind::Ble,event.address,event.address_type,
        payloadValid && transmitterName?event.name:"",blePayload?event.raw:nullptr,
        blePayload?event.length:0,blePayload?event.ble_adv_length:0);
    alertAllowed=rule.alert;
    const unsigned category=unsigned(event.detection.category);
    if(!event.detection.matched() || category>14 || !(config.recon_categories&(1u<<category))) return;
    ++reconTotal;
  }
  logObservation(event);
  // Preserve every enabled match in the log and bounded summary cache; display
  // and buzzer thresholds remain independent when building the view/alert.
  char identity[24]; strlcpy(identity,event.address,sizeof(identity));
  if(event.kind==Kind::Lora) {
    lt::MeshHeader header;
    if(lt::parseMeshHeader(event.raw,event.length,header)) snprintf(identity,sizeof(identity),"?%08lx",(unsigned long)header.from);
    else {
      // Packet fingerprint only, never a claimed LoRa device identity.
      uint32_t hash=2166136261u;
      for(unsigned i=0;i<event.length;i++) hash=(hash^event.raw[i])*16777619u;
      snprintf(identity,sizeof(identity),"raw:%08lx",(unsigned long)hash);
    }
  }
  Summary* selected=nullptr;
  Summary* oldest=&summaries[0];
  for(auto& summary:summaries) {
    if(summary.used && summary.kind==event.kind && summary.addressType==event.address_type && !strcmp(summary.id,identity)) { selected=&summary; break; }
    if(!summary.used) oldest=&summary;
    else if(oldest->used && summary.last<oldest->last) oldest=&summary;
  }
  if(!selected) { selected=oldest; *selected=Summary{}; selected->used=true; selected->kind=event.kind; selected->addressType=event.address_type; strlcpy(selected->id,identity,sizeof(selected->id)); }
  selected->last=event.mono_ms; selected->rssi=event.rssi; ++selected->seen;
  strlcpy(selected->name,event.name,sizeof(selected->name));
  if(event.mode==CaptureMode::Recon) {
    selected->category=event.detection.category;
    selected->confidence=event.detection.confidence;
    strlcpy(selected->label,event.detection.label,sizeof(selected->label));
    strlcpy(selected->evidence,event.detection.evidence,sizeof(selected->evidence));
    emitDetectionAlert(event,alertAllowed,millis());
  }
}
void drain(unsigned limit=48) {
  Observation event;
  while(limit-- && xQueueReceive(captureQueue,static_cast<CaptureObservation*>(&event),0)==pdTRUE) {
    // The ingress copy intentionally excludes classification. A reused record
    // must never carry a previous Recon result into Wardriver or truncated BLE.
    event.detection=recon::Result{};
    consume(event);
  }
}
// Keep the complete marker off the caller's frame while it drains Recon.
// The writer copies it synchronously into its queue; no pointer escapes.
__attribute__((noinline)) bool logSessionMarker(Kind kind,CaptureMode mode,uint32_t id) {
  Observation marker;
  marker.kind=kind; marker.mode=mode; marker.session=id;
  marker.gps=currentFix(); marker.mono_ms=esp_timer_get_time()/1000;
  return logObservation(marker);
}
void requestSession() {
  if(shutdownPending) { strlcpy(model.notice,"Shutdown in progress",sizeof(model.notice)); return; }
  if(quietActive) { strlcpy(model.notice,"GNSS quiet test; restart to capture",sizeof(model.notice)); return; }
  if(capturing.load()) {
    alertPlayer.setEnabled(false);
    endCapture();
    scannersStop();
    drain();
    stopping=true;
    strlcpy(model.notice,"Finishing SD writes...",sizeof(model.notice));
    return;
  }
  if(uploaderBusy()) {
    uploaderCancel(); startAfterUpload=true;
    strlcpy(model.notice,"Cancelling upload before capture",sizeof(model.notice)); return;
  }
  if(!ready || stopping || !loggerIdle()) { strlcpy(model.notice,"Not ready; check diagnostics",sizeof(model.notice)); return; }
  if(!scannersCaptureReady()) { strlcpy(model.notice,"BLE unavailable; restart before capture",sizeof(model.notice)); return; }
  if(gnssControlPending()) { strlcpy(model.notice,"GPS restarting; try Start shortly",sizeof(model.notice)); return; }
  // Missing card is recoverable: require mount before recording a session.
  if(!storageReady()) { strlcpy(model.notice,"Insert SD, then Setup > Retry SD",sizeof(model.notice)); return; }
  uint32_t id=esp_random(); if(!id) id=1;
  sessionId=id;
  const CaptureMode mode=model.recon?CaptureMode::Recon:CaptureMode::Wardriver;
  if(!logSessionMarker(Kind::Start,mode,id)) return;
  memset(totals,0,sizeof(totals));
  reconTotal=0;
  for(auto& summary:summaries) summary=Summary{};
  if(mode==CaptureMode::Recon) {alertHistory.reset();alertHistoryNotice=false;lastAlert=0;}
  startCapture(id,mode);
  serviceAlerts();
  scannersStart(); model.notice[0]=0;
}
void selectMode(bool recon) {
  if(model.recon==recon) return;
  alertPlayer.setEnabled(false);
  // A mode switch closes the previous session before a new Start is accepted.
  // Its stop marker still uses captureMode, not the newly displayed selection.
  if(capturing.load()) requestSession();
  model.recon=recon;
  memset(totals,0,sizeof(totals)); reconTotal=0;
  for(auto& summary:summaries) summary=Summary{};
  strlcpy(model.notice,stopping?"Closing old session; wait to Start":"Mode selected. Press Start.",sizeof(model.notice));
}
void performActions() {
  Action action;
  if(xQueueReceive(actions,&action,0)!=pdTRUE) return;
  if(shutdownPending && action.action!=ui::Action::Shutdown) {
    volatile char* secret=action.value;
    for(size_t i=0;i<sizeof(action.value);++i) secret[i]=0;
    return;
  }
  switch(action.action) {
    case ui::Action::ToggleAutoUpload:
      if(capturing || stopping || !loggerIdle() || uploaderBusy()) strlcpy(model.notice,"Stop capture/upload before changing",sizeof(model.notice));
      else if(!storageReady()) strlcpy(model.notice,"No SD: upload setting not saved",sizeof(model.notice));
      else {
        xSemaphoreTake(busMutex,portMAX_DELAY);
        const bool ok=saveAutoUpload(action.field,!strcmp(action.value,"1"));
        xSemaphoreGive(busMutex);
        if(ok) uploaderConfigChanged();
        strlcpy(model.notice,ok?"Upload preference saved":"Upload preference save failed",sizeof(model.notice));
      }
      break;
    case ui::Action::UploadNow:
      if(capturing || stopping || !loggerIdle() || !ready) strlcpy(model.notice,"Stop and save before upload",sizeof(model.notice));
      else if(!uploaderRequest(action.field)) strlcpy(model.notice,"Upload unavailable; check settings",sizeof(model.notice));
      break;
    case ui::Action::CancelUpload: uploaderCancel(); startAfterUpload=false; break;
    case ui::Action::ToggleSession: requestSession(); break;
    case ui::Action::ToggleMode: selectMode(action.field==1); break;
    case ui::Action::ToggleAlerts:
      config.buzzer=action.field==1;
      if(!config.buzzer)alertPlayer.setEnabled(false);
      break;
    case ui::Action::SaveRegion:
      if(capturing || stopping || !loggerIdle() || uploaderBusy()) strlcpy(model.notice,"Stop and save before changing region",sizeof(model.notice));
      else if(xSemaphoreTake(busMutex,pdMS_TO_TICKS(300))!=pdTRUE)
        strlcpy(model.notice,"Storage busy; region was not changed",sizeof(model.notice));
      else {
        const auto saved=saveLoRaRegion(action.value,storageReady());
        xSemaphoreGive(busMutex);
        if(!saved.applied) strlcpy(model.notice,"Region save failed; previous kept",sizeof(model.notice));
        else if(!scannersConfigureLoRa()) strlcpy(model.notice,"Region saved; LoRa init failed",sizeof(model.notice));
        else if(!config.lora_enabled) strlcpy(model.notice,"LoRa disabled; Wi-Fi/BLE available",sizeof(model.notice));
        else strlcpy(model.notice,saved.sd_saved?(saved.nvs_saved?"Region saved to device and SD":"Region saved to SD"):
            "Region saved to device",sizeof(model.notice));
      }
      break;
    case ui::Action::Shutdown:
      alertPlayer.setEnabled(false);
      if(!shutdownPending) {
        uploaderCancel(); startAfterUpload=false;
        shutdownHadSession=capturing.load() || stopping || !loggerIdle();
        shutdownErrors=counters.storage_errors.load(); shutdownDrops=counters.log_drops.load();
        model.shutdown_error[0]=0;
        if(capturing.load()) requestSession();
        shutdownPending=true; shutdownAt=millis();
      }
      break;
    case ui::Action::RetryStorage:
      if(capturing || stopping || !loggerIdle() || uploaderBusy()) strlcpy(model.notice,"Stop session before remounting SD",sizeof(model.notice));
      else {
        xSemaphoreTake(busMutex,portMAX_DELAY);
        bool ok=mountStorage(); xSemaphoreGive(busMutex);
        strlcpy(model.notice,ok?"SD ready. Reboot to reload config.":"SD unavailable; check FAT32 card",sizeof(model.notice));
      }
      break;
    case ui::Action::SaveCredential:
      if(capturing || stopping || !loggerIdle() || uploaderBusy()) strlcpy(model.notice,"Stop session before saving keys",sizeof(model.notice));
      else if(!storageReady()) strlcpy(model.notice,"No SD: key was not saved",sizeof(model.notice));
      else {
        xSemaphoreTake(busMutex,portMAX_DELAY);
        bool ok=saveCredential(action.field,action.value); xSemaphoreGive(busMutex);
        if(ok) uploaderConfigChanged();
        strlcpy(model.notice,ok?"Credential saved; see Uploads settings":"Save failed; previous config retained",sizeof(model.notice));
      }
      break;
  }
  volatile char* secret=action.value;
  for(size_t i=0;i<sizeof(action.value);++i) secret[i]=0;
}
void refreshModel() {
  model.running=capturing; model.sd=storageReady(); model.lora_ready=loraReady();
  model.wifi=totals[0]; model.ble=totals[1]; model.lora=totals[2];
  model.drops=counters.ingress_drops+counters.log_drops;
  model.errors=counters.storage_errors+counters.radio_errors+counters.malformed;
  model.nmea_chars=gnssCharacters();
  model.buttons_enabled=config.buttons_enabled; model.recon_alerts=config.buzzer;
  model.auto_wigle=config.auto_wigle; model.auto_wardrift=config.auto_wardrift; model.auto_wdgwars=config.auto_wdgwars;
  model.upload_busy=uploaderBusy(); uploaderStatus(model.upload_status,sizeof(model.upload_status));
  model.region_configured=regionConfigured(); model.region_selection_made=regionSelectionMade();
  strlcpy(model.region,config.region,sizeof(model.region)); model.shutting_down=shutdownPending;
  model.recon_total=reconTotal;
  const auto fix=currentFix(); model.gps=fix.valid; strlcpy(model.utc,fix.utc,sizeof(model.utc));
  const auto gps=gnssDiagnostics();
  if(!gps.bytes) strlcpy(model.gps_detail,"GNSS: waiting for serial data",sizeof(model.gps_detail));
  else if(gps.last_sentence_age_ms>3000) strlcpy(model.gps_detail,"GNSS: no fresh valid sentences",sizeof(model.gps_detail));
  else if(fix.valid) snprintf(model.gps_detail,sizeof(model.gps_detail),"GPS FIX / %u satellites used",fix.satellites);
  else if(gps.antenna_open_age_ms<=3000) strlcpy(model.gps_detail,"NO FIX / receiver: ANTENNA OPEN",sizeof(model.gps_detail));
  else if(gps.gsv_age_ms<=10000 && gps.satellites_visible>=0)
    snprintf(model.gps_detail,sizeof(model.gps_detail),"NO FIX / GSV %d visible / %u used",gps.satellites_visible,fix.satellites);
  else strlcpy(model.gps_detail,"GNSS data OK / acquiring fix",sizeof(model.gps_detail));
  strlcpy(model.phase,stopping?"Saving":scanPhase(),sizeof(model.phase));
  strlcpy(model.profile,config.lora_enabled?config.profile:"OFF: Settings > LoRa region",sizeof(model.profile));
  memset(model.recent,0,sizeof(model.recent));
  for(unsigned type=0;type<3;type++) {
    bool chosen[board::SUMMARY_RECORDS]{};
    for(unsigned line=0;line<4;line++) {
      int pick=-1;
      for(unsigned i=0;i<board::SUMMARY_RECORDS;i++) if(summaries[i].used && unsigned(summaries[i].kind)==type && !chosen[i] && (pick<0 || summaries[i].last>summaries[pick].last)) pick=i;
      if(pick<0) break;
      chosen[pick]=true;
      const Summary& summary=summaries[pick];
      const char* title=summary.name[0]?summary.name:summary.id;
      snprintf(model.recent[type][line],48,"%.25s %ddBm x%lu",title,summary.rssi,(unsigned long)summary.seen);
    }
  }
  memset(model.recon_recent,0,sizeof(model.recon_recent));
  memset(model.recon_evidence,0,sizeof(model.recon_evidence));
  memset(model.recon_categories,0,sizeof(model.recon_categories));
  memset(model.recon_confidence,0,sizeof(model.recon_confidence));
  bool chosen[board::SUMMARY_RECORDS]{};
  for(unsigned line=0;line<8;++line) {
    int pick=-1;
    for(unsigned i=0;i<board::SUMMARY_RECORDS;++i) if(summaries[i].used && summaries[i].category!=recon::Category::None &&
       unsigned(summaries[i].confidence)>=config.recon_display_min && !chosen[i] &&
       (pick<0 || summaries[i].last>summaries[pick].last)) pick=i;
    if(pick<0) break;
    chosen[pick]=true; const auto& summary=summaries[pick];
    snprintf(model.recon_recent[line],48,"%.28s %ddBm x%lu",summary.label,summary.rssi,(unsigned long)summary.seen);
    strlcpy(model.recon_evidence[line],summary.evidence,sizeof(model.recon_evidence[line]));
    model.recon_categories[line]=uint8_t(summary.category);
    model.recon_confidence[line]=uint8_t(summary.confidence);
  }
}
void serialStatus() {
  const auto fix=currentFix();
  Serial.printf("LT STATUS version=%s target=%s uptime_ms=%lu ready=%d running=%d saving=%d sd=%d touch=%d lora=%d gps_chars=%lu gps_fix=%d sats=%u wifi=%lu ble=%lu lora_rx=%lu drops=%lu errors=%lu free_heap=%u mode=%s matches=%lu free_psram=%u quiet=%d min_internal=%u largest_internal=%u loop_stack=%u writer_stack=%lu ui_p95_ms=%lu ui_max_ms=%lu ui_samples=%lu upload=%d input_ok=%d cap_ok=%d init_writer=%d init_scanner=%d init_memory=%d scanner_stage=%s heap_hw=%lu heap_ui=%lu heap_config=%lu heap_before_scan=%lu audio_ok=%d tone_active=%d alerted=%u alert_full=%d alloc_hook=%d alloc_fail=%lu\n",
    TUNNEL_VERSION,board::TARGET_ID,(unsigned long)millis(),ready,capturing.load(),stopping,storageReady(),hw::touchPresent(),loraReady(),gnssCharacters(),fix.valid,fix.satellites,
    (unsigned long)totals[0],(unsigned long)totals[1],(unsigned long)totals[2],
    (unsigned long)(counters.ingress_drops.load()+counters.log_drops.load()),
    (unsigned long)(counters.storage_errors.load()+counters.radio_errors.load()+counters.malformed.load()),ESP.getFreeHeap(),
    model.recon?"recon":"wardriver",(unsigned long)reconTotal,ESP.getFreePsram(),quietActive,
    heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
    unsigned(uxTaskGetStackHighWaterMark(nullptr)),(unsigned long)loggerStackMargin(),
    (unsigned long)ui::inputLatencyP95Ms(),(unsigned long)ui::inputLatencyMaxMs(),(unsigned long)ui::inputLatencySamples(),uploaderBusy(),hw::inputReady(),hw::loraFrontendPresent(),initWriter,initScanner,initMemory,scannersInitStage(),
    (unsigned long)heapAfterHardware,(unsigned long)heapAfterUi,(unsigned long)heapAfterConfig,(unsigned long)heapBeforeScanners,hw::alertAudioReady(),alertPlayer.active(),unsigned(alertHistory.size()),alertHistory.full(),allocationHookReady,(unsigned long)heapAllocationFailureSeen.load(std::memory_order_relaxed));
}
void shutdownCancelled(const char* reason) {
  shutdownPending=false; model.shutting_down=false;
  strlcpy(model.shutdown_error,reason,sizeof(model.shutdown_error));
  if(shutdownTimer) esp_timer_stop(shutdownTimer);
}
void serviceShutdown() {
  if(!shutdownPending) return;
  if(uploaderBusy()) {
    uploaderCancel();
    if(millis()-shutdownAt>30000) shutdownCancelled("Upload cancellation pending; stay awake");
    return;
  }
  const auto decision=shutdownDecision(capturing.load(),stopping,loggerIdle(),shutdownHadSession,
      storageReady(),shutdownErrors,counters.storage_errors.load(),shutdownDrops,counters.log_drops.load(),millis()-shutdownAt);
  if(decision==ShutdownDecision::Wait) return;
  if(decision==ShutdownDecision::StorageError) { shutdownCancelled("SD save failed; device kept awake"); return; }
  if(decision==ShutdownDecision::Timeout) { shutdownCancelled("Save timed out; shutdown cancelled"); return; }
  // Saving is complete. An independent timer recovers a blocked upstream radio
  // teardown by rebooting into an idle session, never by cutting an open SD log.
  if(!shutdownTimer) {
    esp_timer_create_args_t args{};
    args.callback=[](void*) { esp_restart(); }; args.name="shutdown-recovery";
    if(esp_timer_create(&args,&shutdownTimer)!=ESP_OK) { shutdownCancelled("Shutdown timer unavailable"); return; }
  }
  if(esp_timer_start_once(shutdownTimer,15000000)!=ESP_OK) { shutdownCancelled("Shutdown timer unavailable"); return; }
  if(!scannersEnterQuiet()) { ready=false; shutdownCancelled("Radio stop failed; restart device"); return; }
  // Radio shutdown latches capture off until reboot. Failure stays awake.
  ready=false;
  if(xSemaphoreTake(busMutex,pdMS_TO_TICKS(500))!=pdTRUE) { shutdownCancelled("Bus busy; restart device"); return; }
  if(!hw::prepareSleepHardware()) {
    xSemaphoreGive(busMutex); shutdownCancelled("Release USER; retry Power or restart"); return;
  }
  if(!unmountStorageForSleep()) {
    hw::cancelSleepPreparation();
    xSemaphoreGive(busMutex); shutdownCancelled("Storage busy; restart device"); return;
  }
  gnssEnd();
  Serial.println("LT POWER confirmed=1 session_closed=1 sleeping=1 wake=user");
  hw::enterDeepSleep();
}
void enterQuietTest(unsigned seconds=90) {
  if(quietActive || capturing.load() || stopping || !loggerIdle() || uploaderBusy()) {
    Serial.println("LT QUIET refused=1 reason=stop_and_save_first"); return;
  }
  // This independent timer also restores normal firmware if an upstream radio
  // shutdown blocks. No GNSS power, baud, constellation or reset is changed.
  if(!quietTimer) {
    esp_timer_create_args_t args{};
    args.callback=[](void*) { esp_restart(); }; args.name="gnss-quiet-limit";
    if(esp_timer_create(&args,&quietTimer)!=ESP_OK) {
      Serial.println("LT QUIET refused=1 reason=timer_unavailable"); return;
    }
  }
  if(esp_timer_start_once(quietTimer,uint64_t(seconds)*1000000)!=ESP_OK) {
    Serial.println("LT QUIET refused=1 reason=timer_unavailable"); return;
  }
  alertPlayer.setEnabled(false);
  quietActive=true;
  Serial.printf("LT QUIET active=1 seconds=%u shutting_down=1\n",seconds);
  quietRadios=scannersEnterQuiet();
  if(xSemaphoreTake(busMutex,pdMS_TO_TICKS(200))==pdTRUE) {
    hw::display().setBrightness(0);
    hw::display().sleep();
    quietDisplay=true;
    xSemaphoreGive(busMutex);
  }
  Serial.printf("LT QUIET active=1 radio_shutdown=%d display_shutdown=%d seconds=%u\n",quietRadios,quietDisplay,seconds);
}
void serialCommands() {
  // Optional bench control; standalone operation never depends on USB.
  static char command[24]{};
  static size_t length=0;
  static bool overflow=false;
  unsigned budget=64;
  while(budget-- && Serial.available()) {
    const char ch=Serial.read();
    if(ch=='\r') continue;
    if(ch=='\n') {
      command[length]=0;
      if(!overflow) {
        if(shutdownPending && strcmp(command,"tunnel status") && strcmp(command,"tunnel gps"))
          Serial.println("LT POWER refused=1 reason=shutdown_pending");
        else if(quietActive && strcmp(command,"tunnel status") && strcmp(command,"tunnel gps") && strcmp(command,"tunnel resume"))
          Serial.println("LT QUIET refused=1 reason=test_active");
        else if(!strcmp(command,"tunnel quiet")) enterQuietTest();
        else if(!strcmp(command,"tunnel quiet-long")) enterQuietTest(300);
        else if(!strcmp(command,"tunnel resume")) { if(quietActive) ESP.restart(); }
        else if(!strcmp(command,"tunnel start")) { if(!capturing.load()) requestSession(); serialStatus(); }
        else if(!strcmp(command,"tunnel stop")) { if(capturing.load()) requestSession(); serialStatus(); }
        else if(!strcmp(command,"tunnel status")) serialStatus();
        else if(!strcmp(command,"tunnel heap")) {
          if(capturing.load() || stopping || uploaderBusy()) Serial.println("LT HEAP refused=1 reason=stop_and_save_first");
          else { Serial.println("LT HEAP per_region=1"); heap_caps_print_heap_info(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); }
        }
        else if(!strcmp(command,"tunnel recon")) { selectMode(true); serialStatus(); }
        else if(!strcmp(command,"tunnel drive")) { selectMode(false); serialStatus(); }
        else if(!strcmp(command,"tunnel gps-warm") || !strcmp(command,"tunnel gps-cold") ||
                !strcmp(command,"tunnel gps-init") || !strcmp(command,"tunnel gps-version")) {
          const auto operation=!strcmp(command,"tunnel gps-warm")?GnssControl::Warm:
              !strcmp(command,"tunnel gps-cold")?GnssControl::Cold:
              !strcmp(command,"tunnel gps-init")?GnssControl::Constellations:GnssControl::Version;
          if(capturing.load() || stopping || !loggerIdle())
            Serial.println("LT GNSS_CONTROL sent=0 reason=stop_and_save_first");
          else Serial.printf("LT GNSS_CONTROL operation=%u sent=%d acknowledged=0\n",
              unsigned(operation),gnssControl(operation));
        }
        else if(!strcmp(command,"tunnel gps")) {
          const auto d=gnssDiagnostics();
          Serial.printf("LT GPS baud=%lu bytes=%lu checksum_ok=%lu checksum_failed=%lu sentences_with_fix=%lu gga=%lu rmc=%lu gsa=%lu gsv=%lu other=%lu fix_quality=%d rmc_status=%c fix_type=%d satellites_visible=%d sentence_age_ms=%lu location_age_ms=%lu parser_location_valid=%d date_valid=%d time_valid=%d gsv_age_ms=%lu txt=%lu antenna_open_reports=%lu antenna_open_age_ms=%lu output_config_attempted=%d output_config_sent=%d antenna_status=%d antenna_ok_reports=%lu antenna_status_age_ms=%lu\n",
            (unsigned long)d.baud,(unsigned long)d.bytes,(unsigned long)d.checksum_ok,(unsigned long)d.checksum_failed,(unsigned long)d.sentences_with_fix,
            (unsigned long)d.gga,(unsigned long)d.rmc,(unsigned long)d.gsa,(unsigned long)d.gsv,(unsigned long)d.other,d.fix_quality,d.rmc_status,d.fix_type,d.satellites_visible,
            (unsigned long)d.last_sentence_age_ms,(unsigned long)d.location_age_ms,d.parser_location_valid,d.date_valid,d.time_valid,
            (unsigned long)d.gsv_age_ms,(unsigned long)d.txt,(unsigned long)d.antenna_open_reports,(unsigned long)d.antenna_open_age_ms,d.output_config_attempted,d.output_config_sent,
            d.antenna_status,(unsigned long)d.antenna_ok_reports,(unsigned long)d.antenna_status_age_ms);
          for(const auto& sky:d.sky) if(sky.sentences)
            Serial.printf("LT SKY talker=%s sentences=%lu complete_groups=%lu age_ms=%lu visible=%d max_cn0=%d cn0_count=%u positive_cn0_count=%u signal_id=%u\n",
                sky.talker,(unsigned long)sky.sentences,(unsigned long)sky.complete_groups,(unsigned long)sky.age_ms,
                sky.visible,sky.max_cn0,sky.cn0_count,sky.positive_cn0_count,sky.signal_id);
          if(gnssSoftwareVersion()[0]) Serial.printf("LT GNSS_VERSION software=%s\n",gnssSoftwareVersion());
        }
      }
      length=0; overflow=false;
    } else if(length<sizeof(command)-1) command[length++]=ch;
    else overflow=true;
  }
}
}

void setup() {
  allocationHookReady=heap_caps_register_failed_alloc_callback(allocationFailed)==ESP_OK;
  Serial.begin(115200); // No wait for USB: standalone boot is mandatory.
  busMutex=xSemaphoreCreateMutex(); actions=xQueueCreate(4,sizeof(Action));
  captureQueueStorage=static_cast<uint8_t*>(allocatePayload(board::CAPTURE_RECORDS*sizeof(CaptureObservation)));
  if(captureQueueStorage) captureQueue=xQueueCreateStatic(board::CAPTURE_RECORDS,sizeof(CaptureObservation),captureQueueStorage,&captureQueueControl);
  if(!busMutex || !actions || !captureQueue) { Serial.println("Allocation failure; halted"); while(true) delay(1000); }
  const bool displayOK=hw::begin(); hardwareReady=displayOK;
  heapAfterHardware=ESP.getFreeHeap();
  if(displayOK) {
    ui::begin(callback);
    configScratchStartup=true;
    ui::setConfigScratchGuard(configScratchAllowed);
    gnssBegin();
  }
  heapAfterUi=ESP.getFreeHeap();
  const bool memoryOK=ESP.getFlashChipSize()==board::FLASH_BYTES && ESP.getPsramSize()==board::PSRAM_BYTES;
  if(displayOK && mountStorage()) {
    if(!loadConfig() && configLoadState==ConfigLoadState::Invalid)
      strlcpy(model.notice,"Config invalid; choose a LoRa region",sizeof(model.notice));
  } else {
    loadRegionPreference();
    strlcpy(model.notice,"No SD. Setup > Retry SD",sizeof(model.notice));
  }
  configScratchStartup=false;
  heapAfterConfig=ESP.getFreeHeap();
  const bool writerOK=displayOK && loggerBegin();
  heapBeforeScanners=ESP.getFreeHeap();
  const bool scannerOK=writerOK && scannersBegin();
  initWriter=writerOK; initScanner=scannerOK; initMemory=memoryOK;
  uploaderBegin();
  ready=writerOK && scannerOK && memoryOK && displayOK && hw::inputReady();
  if(!writerOK) strlcpy(model.notice,"Writer allocation failed",sizeof(model.notice));
  else if(!scannerOK) strlcpy(model.notice,"Scanner initialization failed",sizeof(model.notice));
  else if(!displayOK) strlcpy(model.notice,"Hardware/display initialization failed",sizeof(model.notice));
  if(!memoryOK) strlcpy(model.notice,"Memory mismatch: verify target",sizeof(model.notice));
  if(!hw::inputReady()) strlcpy(model.notice,"Input hardware unavailable",sizeof(model.notice));
  else if(config.buzzer&&!hw::alertAudioReady())strlcpy(model.notice,"Audio unavailable; capture works",sizeof(model.notice));
  if(configAllocationFailed) strlcpy(model.notice,"Config memory unavailable; check SD",sizeof(model.notice));
  else if(rulesLoadFailed) strlcpy(model.notice,"Custom rules failed; check SD file",sizeof(model.notice));
  if(!regionHardwareCompatible()) strlcpy(model.notice,"Region outside cap band; LoRa off",sizeof(model.notice));
  else if(config.lora_enabled && !hw::loraFrontendPresent()) strlcpy(model.notice,"U214 cap RF switch unavailable",sizeof(model.notice));
  Serial.printf("LORA Tunnel %s | flash %u | PSRAM %u | touch %s | SD %s\n",TUNNEL_VERSION,ESP.getFlashChipSize(),ESP.getPsramSize(),hw::touchPresent()?"OK":"missing",storageReady()?"OK":"missing");
  Serial.printf("Init display=%d writer=%d scanner=%d ready=%d free_heap=%u\n",displayOK,writerOK,scannerOK,ready,ESP.getFreeHeap());
  serialStatus();
  // Diagnostics intentionally never print configuration contents or credentials.
}
void loop() {
  if(!hardwareReady) { serialCommands(); delay(10); return; }
  if(quietActive) { gnssTick(); serialCommands(); delay(5); return; }
  serviceAlerts();
  gnssTick(); scannersTick(); drain(8); performActions(); serialCommands();
  serviceAlerts();
  if(quietActive) { delay(5); return; }
  // Always drain the debounce state, even when navigation is disabled in config.
  ui::navigation(hw::navigationButtons(),model);
  if(board::HAS_KEYBOARD) {
    input::KeyEvent key;
    for(unsigned count=0;count<8 && hw::pollKeyboard(key);++count) ui::keyboard(key,model);
  }
  if(stopping) {
    // Producer callbacks have stopped. Retry the stop marker until queue space exists.
    static bool endQueued=false;
    if(!endQueued) {
      drain();
      const CaptureMode mode=captureMode.load();
      const uint32_t id=sessionId.load();
      endQueued=logSessionMarker(Kind::Stop,mode,id);
    }
    if(endQueued && loggerIdle()) {
      stopping=false; endQueued=false;
      strlcpy(model.notice,storageReady()?"Session saved. Uploads run while idle.":"Storage error: inspect recovered log",sizeof(model.notice));
    }
  }
  static uint32_t lastBattery=0;
  if(millis()-lastBattery>5000) { model.battery=hw::batteryVolts(); lastBattery=millis(); }
  if(startAfterUpload && !uploaderBusy()) { startAfterUpload=false; requestSession(); }
  uploaderTick(ready && !capturing.load() && !stopping && loggerIdle() && !shutdownPending && !startAfterUpload,ui::allowsBackgroundUpload(),ui::allowsUploadJobPolling());
  refreshModel();
  if(!board::DISPLAY_SHARED_WITH_SD) ui::tick(model);
  else if(xSemaphoreTake(busMutex,0)==pdTRUE) { ui::tick(model); xSemaphoreGive(busMutex); }
  else ++counters.ui_skips;
  serviceShutdown();
  delay(5);
}
