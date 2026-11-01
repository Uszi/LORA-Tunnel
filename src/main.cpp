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

using namespace tunnel;
namespace {
ui::Model model;
bool ready=false,stopping=false;
bool quietActive=false,quietRadios=false,quietDisplay=false;
esp_timer_handle_t quietTimer=nullptr;
uint32_t totals[3]{},lastBeep=0,reconTotal=0;
StaticQueue_t captureQueueControl;
uint8_t* captureQueueStorage=nullptr;
struct Action { ui::Action action; int field; char value[128]{}; };
QueueHandle_t actions=nullptr;
struct Summary {
  bool used=false;
  Kind kind=Kind::Wifi;
  uint8_t addressType=255;
  char id[24]{},name[40]{},label[32]{},evidence[128]{};
  recon::Category category=recon::Category::None;
  recon::Confidence confidence=recon::Confidence::None;
  int rssi=0;
  uint32_t seen=0,alertAt=0;
  uint64_t last=0;
};
Summary summaries[64];

void callback(ui::Action action,int field,const char* value) {
  Action request{}; request.action=action; request.field=field;
  strlcpy(request.value,value,sizeof(request.value));
  if(xQueueSend(actions,&request,0)!=pdTRUE) strlcpy(model.notice,"Busy: please try again",sizeof(model.notice));
  memset(request.value,0,sizeof(request.value));
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
    const uint32_t now=millis();
    if(config.buzzer && alertAllowed && unsigned(event.detection.confidence)>=config.recon_alert_min &&
       (selected->alertAt==0 || now-selected->alertAt>=30000) && now-lastBeep>=1000) {
      hw::beep(); selected->alertAt=lastBeep=now;
    }
  }
}
void drain(unsigned limit=48) {
  Observation event;
  while(limit-- && xQueueReceive(captureQueue,&event,0)==pdTRUE) consume(event);
}
void requestSession() {
  if(quietActive) { strlcpy(model.notice,"GNSS quiet test; restart to capture",sizeof(model.notice)); return; }
  if(capturing.load()) {
    endCapture();
    scannersStop();
    drain();
    stopping=true;
    strlcpy(model.notice,"Finishing SD writes...",sizeof(model.notice));
    return;
  }
  if(!ready || stopping || !loggerIdle()) { strlcpy(model.notice,"Not ready; check diagnostics",sizeof(model.notice)); return; }
  // Missing card is recoverable: require mount before recording a session.
  if(!storageReady()) { strlcpy(model.notice,"Insert SD, then Setup > Retry SD",sizeof(model.notice)); return; }
  uint32_t id=esp_random(); if(!id) id=1;
  sessionId=id;
  const CaptureMode mode=model.recon?CaptureMode::Recon:CaptureMode::Wardriver;
  Observation start; start.kind=Kind::Start; start.mode=mode; start.session=id; start.gps=currentFix(); start.mono_ms=esp_timer_get_time()/1000;
  if(!logObservation(start)) return;
  memset(totals,0,sizeof(totals));
  reconTotal=0;
  for(auto& summary:summaries) summary=Summary{};
  startCapture(id,mode);
  scannersStart(); model.notice[0]=0;
}
void selectMode(bool recon) {
  if(model.recon==recon) return;
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
  switch(action.action) {
    case ui::Action::ToggleSession: requestSession(); break;
    case ui::Action::ToggleMode: selectMode(action.field==1); break;
    case ui::Action::ToggleAlerts: config.buzzer=action.field==1; break;
    case ui::Action::RetryStorage:
      if(capturing || stopping || !loggerIdle()) strlcpy(model.notice,"Stop session before remounting SD",sizeof(model.notice));
      else {
        xSemaphoreTake(busMutex,portMAX_DELAY);
        bool ok=mountStorage(); xSemaphoreGive(busMutex);
        strlcpy(model.notice,ok?"SD ready. Reboot to reload config.":"SD unavailable; check FAT32 card",sizeof(model.notice));
      }
      break;
    case ui::Action::SaveCredential:
      if(capturing || stopping || !loggerIdle()) strlcpy(model.notice,"Stop session before saving keys",sizeof(model.notice));
      else if(!storageReady()) strlcpy(model.notice,"No SD: key was not saved",sizeof(model.notice));
      else {
        xSemaphoreTake(busMutex,portMAX_DELAY);
        bool ok=saveCredential(action.field,action.value); xSemaphoreGive(busMutex);
        strlcpy(model.notice,ok?"Key saved. Uploads not yet enabled.":"Save failed; previous config retained",sizeof(model.notice));
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
  strlcpy(model.profile,config.lora_enabled?config.profile:"OFF: check SD config",sizeof(model.profile));
  memset(model.recent,0,sizeof(model.recent));
  for(unsigned type=0;type<3;type++) {
    bool chosen[64]{};
    for(unsigned line=0;line<4;line++) {
      int pick=-1;
      for(unsigned i=0;i<64;i++) if(summaries[i].used && unsigned(summaries[i].kind)==type && !chosen[i] && (pick<0 || summaries[i].last>summaries[pick].last)) pick=i;
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
  bool chosen[64]{};
  for(unsigned line=0;line<8;++line) {
    int pick=-1;
    for(unsigned i=0;i<64;++i) if(summaries[i].used && summaries[i].category!=recon::Category::None &&
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
  Serial.printf("LT STATUS version=%s uptime_ms=%lu ready=%d running=%d saving=%d sd=%d touch=%d lora=%d gps_chars=%lu gps_fix=%d sats=%u wifi=%lu ble=%lu lora_rx=%lu drops=%lu errors=%lu free_heap=%u mode=%s matches=%lu free_psram=%u quiet=%d\n",
    TUNNEL_VERSION,(unsigned long)millis(),ready,capturing.load(),stopping,storageReady(),hw::touchPresent(),loraReady(),gnssCharacters(),fix.valid,fix.satellites,
    (unsigned long)totals[0],(unsigned long)totals[1],(unsigned long)totals[2],
    (unsigned long)(counters.ingress_drops.load()+counters.log_drops.load()),
    (unsigned long)(counters.storage_errors.load()+counters.radio_errors.load()+counters.malformed.load()),ESP.getFreeHeap(),
    model.recon?"recon":"wardriver",(unsigned long)reconTotal,ESP.getFreePsram(),quietActive);
}
void enterQuietTest() {
  if(quietActive || capturing.load() || stopping || !loggerIdle()) {
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
  if(esp_timer_start_once(quietTimer,90000000)!=ESP_OK) {
    Serial.println("LT QUIET refused=1 reason=timer_unavailable"); return;
  }
  quietActive=true;
  Serial.println("LT QUIET active=1 seconds=90 shutting_down=1");
  quietRadios=scannersEnterQuiet();
  if(xSemaphoreTake(busMutex,pdMS_TO_TICKS(200))==pdTRUE) {
    hw::display().setBrightness(0);
    hw::display().sleep();
    quietDisplay=true;
    xSemaphoreGive(busMutex);
  }
  Serial.printf("LT QUIET active=1 radio_shutdown=%d display_shutdown=%d seconds=90\n",quietRadios,quietDisplay);
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
        if(quietActive && strcmp(command,"tunnel status") && strcmp(command,"tunnel gps") && strcmp(command,"tunnel resume"))
          Serial.println("LT QUIET refused=1 reason=test_active");
        else if(!strcmp(command,"tunnel quiet")) enterQuietTest();
        else if(!strcmp(command,"tunnel resume")) { if(quietActive) ESP.restart(); }
        else if(!strcmp(command,"tunnel start")) { if(!capturing.load()) requestSession(); serialStatus(); }
        else if(!strcmp(command,"tunnel stop")) { if(capturing.load()) requestSession(); serialStatus(); }
        else if(!strcmp(command,"tunnel status")) serialStatus();
        else if(!strcmp(command,"tunnel recon")) { selectMode(true); serialStatus(); }
        else if(!strcmp(command,"tunnel drive")) { selectMode(false); serialStatus(); }
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
        }
      }
      length=0; overflow=false;
    } else if(length<sizeof(command)-1) command[length++]=ch;
    else overflow=true;
  }
}
}

void setup() {
  Serial.begin(115200); // No wait for USB: standalone boot is mandatory.
  busMutex=xSemaphoreCreateMutex(); actions=xQueueCreate(4,sizeof(Action));
  captureQueueStorage=static_cast<uint8_t*>(heap_caps_malloc(48*sizeof(Observation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(captureQueueStorage) captureQueue=xQueueCreateStatic(48,sizeof(Observation),captureQueueStorage,&captureQueueControl);
  if(!busMutex || !actions || !captureQueue) { Serial.println("Allocation failure; halted"); while(true) delay(1000); }
  const bool displayOK=hw::begin();
  ui::begin(callback);
  gnssBegin();
  const bool memoryOK=ESP.getFlashChipSize()==board::FLASH_BYTES && ESP.getPsramSize()==board::PSRAM_BYTES;
  if(mountStorage()) {
    if(!loadConfig()) strlcpy(model.notice,config.lora_enabled?"US defaults; SD config optional":"Config invalid; LoRa disabled",sizeof(model.notice));
  } else strlcpy(model.notice,"No SD. Setup > Retry SD",sizeof(model.notice));
  const bool writerOK=loggerBegin();
  const bool scannerOK=writerOK && scannersBegin();
  ready=writerOK && scannerOK && memoryOK && displayOK;
  if(!writerOK) strlcpy(model.notice,"Writer allocation failed",sizeof(model.notice));
  else if(!scannerOK) strlcpy(model.notice,"Scanner initialization failed",sizeof(model.notice));
  else if(!displayOK) strlcpy(model.notice,"Display initialization failed",sizeof(model.notice));
  if(!memoryOK) strlcpy(model.notice,"Memory mismatch: verify V4-R8",sizeof(model.notice));
  if(!hw::touchPresent()) strlcpy(model.notice,"Touch not found at 0x2E",sizeof(model.notice));
  Serial.printf("LORA Tunnel %s | flash %u | PSRAM %u | touch %s | SD %s\n",TUNNEL_VERSION,ESP.getFlashChipSize(),ESP.getPsramSize(),hw::touchPresent()?"OK":"missing",storageReady()?"OK":"missing");
  Serial.printf("Init display=%d writer=%d scanner=%d ready=%d free_heap=%u\n",displayOK,writerOK,scannerOK,ready,ESP.getFreeHeap());
  serialStatus();
  // Diagnostics intentionally never print configuration contents or credentials.
}
void loop() {
  if(quietActive) { gnssTick(); serialCommands(); delay(5); return; }
  gnssTick(); scannersTick(); drain(8); performActions(); serialCommands();
  if(quietActive) { delay(5); return; }
  // Always drain the debounce state, even when navigation is disabled in config.
  ui::navigation(hw::navigationButtons(),model);
  if(stopping) {
    // Producer callbacks have stopped. Retry the stop marker until queue space exists.
    static bool endQueued=false;
    if(!endQueued) {
      drain();
      Observation end; end.kind=Kind::Stop; end.mode=captureMode.load(); end.session=sessionId; end.gps=currentFix(); end.mono_ms=esp_timer_get_time()/1000;
      endQueued=logObservation(end);
    }
    if(endQueued && loggerIdle()) {
      stopping=false; endQueued=false;
      strlcpy(model.notice,storageReady()?"Session closed. SD safe to remove.":"Storage error: inspect recovered log",sizeof(model.notice));
    }
  }
  static uint32_t lastBattery=0;
  if(millis()-lastBattery>5000) { model.battery=hw::batteryVolts(); lastBattery=millis(); }
  refreshModel();
  if(xSemaphoreTake(busMutex,0)==pdTRUE) { ui::tick(model); xSemaphoreGive(busMutex); }
  else ++counters.ui_skips;
  delay(5);
}
