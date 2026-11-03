#if defined(LORA_TUNNEL_CARDPUTER_ADV)
#include "ui.h"
#include "hardware.h"
#include "lora_profiles.h"
#include "version.h"
#include "credential_validation.h"
#include "cardputer_logo_pixels.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr int WIDTH=240, HEIGHT=135;
constexpr uint16_t BG=0x0021, PANEL=0x0843, GREEN=0x77e0, CYAN=0x07ff;
constexpr uint16_t WHITE=0xef7d, MUTED=0x94b2, AMBER=0xfd20;
constexpr const char* fields[]={"WiGLE API name","WiGLE API token","WDGWars token",
  "Home Wi-Fi SSID","Home Wi-Fi password","Wardrift API token"};
constexpr const char* categories[]={"ALL","ALPR","FLOCK","BODYCAM","IBEACON","RING",
  "CAMERAS","DRONES","FIND MY","TRACKERS","GLASSES","ACOUSTIC","SERIAL","RADIO TOOLS","CUSTOM"};
enum class Page { Home, Drive, Recon, Detail, Settings, Credentials, Edit, Regions, Power, Uploads };
Page page=Page::Home, settingsParent=Page::Home;
ui::Handler handler=nullptr;
lgfx::LGFX_Sprite canvas;
void* pixels=nullptr;
bool buffered=false, dirty=true, splashShown=false, onboarded=false;
bool regionOnboarding=false;
uint32_t splashAt=0, paintedAt=0;
uint32_t inputAt=0, inputMaximum=0, latencySamples[64]{};
uint32_t latencyTotal=0;
unsigned latencyCount=0, latencyNext=0;
bool inputPending=false;
unsigned focus=0, tab=0, filter=0, selectedMatch=0, editField=0;
int regionIndex=0;
char inputValue[128]{};
char editError[64]{};
size_t inputLength=0;
struct Detail { char title[48]{}, evidence[64]{}; uint8_t category=0, confidence=0; } detail;

lgfx::LovyanGFX& gfx() { return buffered?static_cast<lgfx::LovyanGFX&>(canvas):hw::display(); }
void emit(ui::Action action,int field=0,const char* value="") { if(handler) handler(action,field,value); }
void eraseInput() { volatile char* p=inputValue; for(size_t i=0;i<sizeof(inputValue);++i)p[i]=0; inputLength=0; }
void show(Page next) { page=next; focus=0; dirty=true; }
void label(const char* value,int x,int y,uint16_t color=WHITE,size_t limit=38,int scale=1) {
  char text[80]{};
  snprintf(text,sizeof(text),"%.*s",int(std::min(limit,sizeof(text)-1)),value);
  gfx().setTextSize(scale); gfx().setTextColor(color);
  gfx().setTextDatum(lgfx::textdatum_t::top_left); gfx().drawString(text,x,y);
}
void row(int y,const char* text,bool selected,uint16_t ink=WHITE) {
  gfx().fillRoundRect(3,y,234,17,2,selected?GREEN:PANEL);
  label(selected?">":" ",6,y+4,selected?BG:ink,1);
  label(text,16,y+4,selected?BG:ink,36);
}
void footer(const char* keys,const ui::Model& m,bool notice=true) {
  gfx().fillRect(0,121,WIDTH,14,PANEL);
  label(keys,3,125,CYAN,39);
  if(notice && m.notice[0]) label(m.notice,3,110,AMBER,39);
}
// Draw flash pixels in same-color horizontal runs. The canvas stays 32400
// bytes; no decompressor, image sprite, or temporary image allocation is used.
void logo(int x,int y,unsigned width,unsigned height,const uint16_t* data) {
  for(unsigned row=0;row<height;++row) {
    for(unsigned start=0;start<width;) {
      const uint16_t color=data[row*width+start];
      unsigned end=start+1;
      while(end<width && data[row*width+end]==color)++end;
      gfx().drawFastHLine(x+int(start),y+int(row),int(end-start),color);
      start=end;
    }
  }
}
void header(const char* title) {
  gfx().fillScreen(BG);
  label("LORA TUNNEL",4,3,GREEN,11,2);
  label(title,142,7,CYAN,12);
  logo(220,0,cardputer_assets::vtsWidth,cardputer_assets::vtsHeight,cardputer_assets::vtsPixels);
  gfx().drawFastHLine(3,21,234,GREEN);
}
void status(const ui::Model& m) {
  char line[48];
  if(std::isfinite(m.battery) && m.battery>0)
    snprintf(line,sizeof(line),"SD:%s GPS:%s %.2fV %s",m.sd?"OK":"--",m.gps?"FIX":"--",m.battery,m.running?"REC":"IDLE");
  else snprintf(line,sizeof(line),"SD:%s GPS:%s BAT:-- %s",m.sd?"OK":"--",m.gps?"FIX":"--",m.running?"REC":"IDLE");
  label(line,4,26,m.sd?WHITE:AMBER,38);
}
unsigned matches(const ui::Model& m,uint8_t indices[8]) {
  unsigned count=0;
  for(uint8_t i=0;i<8;++i) if(m.recon_recent[i][0] && m.recon_categories[i]>0 &&
      m.recon_categories[i]<15 && (!filter || filter==m.recon_categories[i])) indices[count++]=i;
  return count;
}
void openMode(bool recon,const ui::Model& m) {
  if(m.recon!=recon) emit(ui::Action::ToggleMode,recon?1:0);
  show(recon?Page::Recon:Page::Drive);
}
void openSettings() { settingsParent=page; show(Page::Settings); }
void openRegions(const ui::Model& m,bool first) {
  regionOnboarding=first; regionIndex=0;
  for(size_t i=0;i<tunnel::lora_profiles::profileCount;++i)
    if(!strcmp(m.region,tunnel::lora_profiles::profiles[i].code)) regionIndex=int(i);
  show(Page::Regions);
}
void back(const ui::Model& m) {
  if(page==Page::Power) { show(Page::Settings); return; }
  if(page==Page::Edit) { eraseInput(); show(Page::Credentials); return; }
  if(page==Page::Credentials || page==Page::Uploads) { show(Page::Settings); return; }
  if(page==Page::Regions) {
    if(regionOnboarding && !m.running) emit(ui::Action::SaveRegion,0,"UNSET");
    show(regionOnboarding?Page::Home:Page::Settings); return;
  }
  if(page==Page::Settings) { show(settingsParent); return; }
  if(page==Page::Detail) { show(Page::Recon); return; }
  show(Page::Home);
}
unsigned menuCount() {
  if(page==Page::Home)return 3;
  if(page==Page::Settings)return 6;
  if(page==Page::Credentials)return sizeof(fields)/sizeof(fields[0]);
  if(page==Page::Uploads)return 7;
  if(page==Page::Power)return 2;
  return 0;
}
void select(const ui::Model& m) {
  if(page==Page::Home) {
    if(focus<2)openMode(focus==1,m); else openSettings();
  } else if(page==Page::Settings) {
    if(focus==0)openRegions(m,false);
    else if(focus==1)show(Page::Credentials);
    else if(focus==2)show(Page::Uploads);
    else if(focus==3)emit(ui::Action::ToggleAlerts,m.recon_alerts?0:1);
    else if(focus==4)emit(ui::Action::RetryStorage);
    else show(Page::Power);
  } else if(page==Page::Credentials) {
    editField=focus; eraseInput(); editError[0]=0; show(Page::Edit);
  } else if(page==Page::Edit) {
    if(!tunnel::validCredential(int(editField),inputValue)) {
      const char* error=editField==3?"SSID: at most 32 bytes":editField==4?"Password: 8-63 chars, 64 hex, or blank":editField==0?"API name: no colon or control chars":"Use printable text, at most 127 bytes";
      snprintf(editError,sizeof(editError),"%s",error);return;
    }
    emit(ui::Action::SaveCredential,int(editField),inputValue); eraseInput(); show(Page::Credentials);
  } else if(page==Page::Regions) {
    const auto& profile=tunnel::lora_profiles::profiles[regionIndex];
    if(!m.running && tunnel::lora_profiles::selectable(profile)) {
      emit(ui::Action::SaveRegion,0,profile.code); show(regionOnboarding?Page::Home:Page::Settings);
    }
  } else if(page==Page::Power) {
    if(focus==0)show(Page::Settings); else emit(ui::Action::Shutdown);
  } else if(page==Page::Uploads) {
    const bool enabled[]={m.auto_wigle,m.auto_wardrift,m.auto_wdgwars};
    if(focus<3)emit(ui::Action::ToggleAutoUpload,int(focus),enabled[focus]?"0":"1");
    else if(focus==6)emit(ui::Action::CancelUpload);
    else if(!m.running && !m.upload_busy)emit(ui::Action::UploadNow,int(focus-3));
  } else if(page==Page::Drive) {
    if(!m.recon)emit(ui::Action::ToggleSession);
  } else if(page==Page::Recon) {
    uint8_t indices[8]; const unsigned count=matches(m,indices);
    if(count) {
      selectedMatch=std::min(selectedMatch,count-1); const uint8_t i=indices[selectedMatch];
      memcpy(detail.title,m.recon_recent[i],sizeof(detail.title)); detail.title[sizeof(detail.title)-1]=0;
      memcpy(detail.evidence,m.recon_evidence[i],sizeof(detail.evidence)); detail.evidence[sizeof(detail.evidence)-1]=0;
      detail.category=m.recon_categories[i]; detail.confidence=m.recon_confidence[i]; show(Page::Detail);
    }
  } else if(page==Page::Detail)show(Page::Recon);
}
void move(int delta,const ui::Model& m) {
  if(page==Page::Regions) {
    const int count=int(tunnel::lora_profiles::profileCount);
    regionIndex=(regionIndex+delta+count)%count;
  } else if(page==Page::Recon) {
    uint8_t indices[8]; const unsigned count=matches(m,indices);
    if(count)selectedMatch=(selectedMatch+count+delta)%count;
  } else {
    const unsigned count=menuCount(); if(count)focus=(focus+count+delta)%count;
  }
}
void drawMenu(const char* const* names,unsigned count) {
  const unsigned start=(focus/4)*4;
  if(count>4) {char pages[12];snprintf(pages,sizeof(pages),"%u/%u",focus/4+1,(count+3)/4);label(pages,198,7,MUTED,3);}
  for(unsigned i=0;i<4 && start+i<count;++i)row(36+int(i)*18,names[start+i],focus==start+i);
}
void wrapped(const char* text,int y,size_t capacity,uint16_t color) {
  const size_t length=strnlen(text,capacity);
  for(size_t offset=0;offset<length;offset+=38)label(text+offset,5,y+int(offset/38)*10,color,std::min(size_t{38},length-offset));
}
void render(const ui::Model& m) {
  char text[80];
  if(m.shutting_down || page==Page::Power) {
    header("POWER");
    label(m.shutting_down?"Saving session before sleep...":"Stop capture, save, then sleep?",4,30,AMBER,38);
    label("G0 wakes. Save before using switch.",4,44,MUTED,38);
    if(m.shutting_down)label("Please wait for SD writes.",4,72,WHITE,38);
    else { row(64,"CANCEL",focus==0); row(84,"SAVE + SLEEP",focus==1,AMBER); }
    if(m.shutdown_error[0])label(m.shutdown_error,4,109,AMBER,38);
    footer(m.shutting_down?"Saving...":"Up/Down choose  Enter OK  Esc cancel",m,false); return;
  }
  if(page==Page::Home) {
    header("HOME"); status(m);
    const char* names[]={"WARDRIVER / Wi-Fi + BLE + LoRa","RECON / indicators + evidence","SETTINGS / device + uploads"};
    drawMenu(names,3);
    label(m.gps_detail[0]?m.gps_detail:"GNSS: waiting for data / fix",4,96,MUTED,38);
    footer("W Drive  R Recon  S Setup  Enter open",m);
  } else if(page==Page::Drive) {
    header("DRIVE"); status(m);
    const uint32_t totals[]={m.wifi,m.ble,m.lora}; const char* names[]={"Wi-Fi","BLE","LoRa"};
    snprintf(text,sizeof(text),"< %s %lu >  %s",names[tab],(unsigned long)totals[tab],m.phase); label(text,4,39,GREEN,38);
    for(int i=0;i<4;++i)label(m.recent[tab][i],4,51+i*11,WHITE,38);
    if(!m.recent[tab][0][0])label(tab==2&&!m.lora_ready?"LoRa off: check region / Cap":"No observations yet",4,64,MUTED,38);
    if(tab==2 && !m.drops && !m.errors)snprintf(text,sizeof(text),"RX %s",m.profile[0]?m.profile:"Choose LoRa region");
    else snprintf(text,sizeof(text),"Drop:%lu Err:%lu  %s",(unsigned long)m.drops,(unsigned long)m.errors,m.gps?"GNSS FIX":"NO FIX");
    label(text,4,97,(m.drops||m.errors)?AMBER:MUTED,38);
    footer("Space Start/Stop  Left/Right  S Setup",m);
  } else if(page==Page::Recon) {
    header("RECON"); status(m);
    snprintf(text,sizeof(text),"< %s > %lu matches %s",categories[filter],(unsigned long)m.recon_total,m.recon_alerts?"SOUND":"MUTE");
    label(text,4,39,CYAN,38);
    uint8_t indices[8]; const unsigned count=matches(m,indices);
    if(count) {
      selectedMatch=std::min(selectedMatch,count-1); const unsigned start=selectedMatch/3*3;
      for(unsigned i=0;i<3 && start+i<count;++i)row(51+int(i)*18,m.recon_recent[indices[start+i]],selectedMatch==start+i);
    } else {label("No matches in recent window",4,61,MUTED,38); label("A match is evidence, not identity.",4,78,MUTED,38);}
    footer("Space Run  <> Filter  Enter detail  M",m);
  } else if(page==Page::Detail) {
    header("EVIDENCE");
    snprintf(text,sizeof(text),"%s / %s",categories[std::min(unsigned(detail.category),14U)],detail.confidence>=3?"STRONG SIGNATURE":detail.confidence==2?"MEDIUM EVIDENCE":"WEAK INDICATOR");
    label(text,4,27,CYAN,38); wrapped(detail.title,42,sizeof(detail.title),WHITE);
    wrapped(detail.evidence[0]?detail.evidence:"No further evidence reported",67,sizeof(detail.evidence),WHITE);
    label("Evidence does not confirm identity.",4,105,MUTED,38); footer("Enter / Esc return to matches",m,false);
  } else if(page==Page::Settings) {
    header("SETTINGS"); status(m);
    char alert[40];snprintf(alert,sizeof(alert),"Alert sound: %s",m.recon_alerts?"ON":"OFF");
    const char* names[]={"LoRa region","Connection keys", "Automatic uploads",alert,"Retry SD card","Power / save + sleep"};
    drawMenu(names,6); footer("Up/Down choose  Enter open  Esc back",m);
  } else if(page==Page::Credentials) {
    header("KEYS"); label("Enter a replacement; saved on SD.",4,26,MUTED,38);
    drawMenu(fields,sizeof(fields)/sizeof(fields[0])); footer("Up/Down choose  Enter edit  Esc back",m);
  } else if(page==Page::Edit) {
    header("EDIT KEY"); label(fields[editField],4,27,GREEN,38);
    gfx().fillRoundRect(3,43,234,25,3,PANEL);
    char view[38]{}; const size_t visible=std::min(inputLength,size_t{37});
    const bool secret=editField==1 || editField==2 || editField==4 || editField==5;
    if(secret)memset(view,'*',visible); else memcpy(view,inputValue+inputLength-visible,visible);
    label(inputLength?view:"Type on the keyboard...",8,52,inputLength?WHITE:MUTED,37);
    snprintf(text,sizeof(text),"%u / 127 characters",unsigned(inputLength));label(text,4,75,MUTED,38);
    label("Shift / Fn symbols; Backspace deletes",4,90,MUTED,38);
    footer("Enter save to SD  Esc cancel",m,!editError[0]);
    if(editError[0])label(editError,4,110,AMBER,38);
  } else if(page==Page::Regions) {
    header("REGION");label("U214: 868-923 MHz LoRa receive",4,27,CYAN,38);
    const unsigned start=unsigned(regionIndex)/3*3;
    for(unsigned i=0;i<3 && start+i<tunnel::lora_profiles::profileCount;++i) {
      const auto& p=tunnel::lora_profiles::profiles[start+i];const bool available=tunnel::lora_profiles::selectable(p);
      snprintf(text,sizeof(text),"%s%s",available?"":"[X] ",p.label);row(39+int(i)*18,text,int(start+i)==regionIndex,available?WHITE:MUTED);
    }
    const auto& p=tunnel::lora_profiles::profiles[regionIndex];
    if(m.running)snprintf(text,sizeof(text),"Stop capture before changing region");
    else if(tunnel::lora_profiles::selectable(p))snprintf(text,sizeof(text),"LongFast %.3f MHz - Enter saves",p.frequency_mhz);
    else snprintf(text,sizeof(text),"Outside U214 supported radio band");
    label(text,4,98,tunnel::lora_profiles::selectable(p)?GREEN:AMBER,38);
    footer(regionOnboarding?"Up/Down choose  Enter save  Esc skip":"Up/Down choose  Enter save  Esc back",m);
  } else if(page==Page::Uploads) {
    header("UPLOADS");label(m.upload_busy?"Uploading completed sessions...":"Home Wi-Fi / completed sessions only",4,26,m.upload_busy?AMBER:MUTED,38);
    char wigle[40],wardrift[40],wdgwars[40];
    snprintf(wigle,sizeof(wigle),"WiGLE automatic: %s",m.auto_wigle?"ON":"OFF");
    snprintf(wardrift,sizeof(wardrift),"Wardrift automatic: %s",m.auto_wardrift?"ON":"OFF");
    snprintf(wdgwars,sizeof(wdgwars),"WDGWars automatic: %s",m.auto_wdgwars?"ON":"OFF");
    const char* names[]={wigle,wardrift,wdgwars,"Upload now: WiGLE","Upload now: Wardrift","Upload now: WDGWars","Cancel upload"};drawMenu(names,7);
    if(m.upload_status[0])label(m.upload_status,4,110,AMBER,38);
    footer("Up/Down choose  Enter apply  Esc back",m,false);
  }
}
void splash() {
  header("ADV + U214");
  logo(83,25,cardputer_assets::radioWidth,cardputer_assets::radioHeight,cardputer_assets::radioPixels);
  label("WIFI + BLE + LORA",25,103,GREEN,20,2);
  label(TUNNEL_VERSION,4,125,MUTED,38);
}
void present() {
  if(buffered)canvas.pushSprite(&hw::display(),0,0);
  if(inputPending) {
    const uint32_t elapsed=millis()-inputAt;
    inputMaximum=std::max(inputMaximum,elapsed);
    latencySamples[latencyNext]=elapsed;latencyNext=(latencyNext+1)%64;
    latencyCount=std::min(latencyCount+1,64U);inputPending=false;
    ++latencyTotal;
  }
}
}

namespace ui {
void begin(Handler onAction) {
  handler=onAction;page=settingsParent=Page::Home;focus=tab=filter=selectedMatch=editField=0;
  dirty=true;splashShown=onboarded=false;regionOnboarding=false;eraseInput();
  inputPending=false;inputMaximum=latencyCount=latencyNext=latencyTotal=0;
  if(!pixels)pixels=heap_caps_malloc(WIDTH*HEIGHT,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  buffered=pixels!=nullptr;
  if(buffered) {canvas.setColorDepth(8);canvas.setBuffer(pixels,WIDTH,HEIGHT,8);}
  gfx().setFont(&lgfx::fonts::Font0);gfx().setTextWrap(false,false);

}
void navigation(uint8_t,const Model&) {} // Cardputer keyboard is the primary input.
void keyboard(const input::KeyEvent& event,const Model& model) {
  if(model.shutting_down || !splashShown || millis()-splashAt<1000)return;
  if(event.code==input::KeyCode::None)return;
  if(!inputPending) {inputPending=true;inputAt=millis();}
  dirty=true;
  using input::KeyCode;KeyCode code=event.code;const char c=event.character;
  if(page==Page::Edit && code==KeyCode::Text) {
    if(c>=32 && c<=126 && inputLength<sizeof(inputValue)-1) {inputValue[inputLength++]=c;inputValue[inputLength]=0;dirty=true;}
    return;
  }
  if(code==KeyCode::Text) {
    if(c==';')code=KeyCode::Up;else if(c=='.')code=KeyCode::Down;
    else if(c==',')code=KeyCode::Left;else if(c=='/')code=KeyCode::Right;
    else if(c=='`')code=KeyCode::Back;
    else if(page==Page::Home && (c=='w'||c=='W')) {openMode(false,model);return;}
    else if(page==Page::Home && (c=='r'||c=='R')) {openMode(true,model);return;}
    else if((page==Page::Home || page==Page::Drive || page==Page::Recon) && (c=='s'||c=='S')) {openSettings();return;}
    else if((page==Page::Drive || page==Page::Recon) && c==' ') {
      if(model.recon==(page==Page::Recon))emit(Action::ToggleSession);dirty=true;return;
    } else if(page==Page::Recon && (c=='m'||c=='M')) {emit(Action::ToggleAlerts,model.recon_alerts?0:1);dirty=true;return;}
    else return;
  }
  if(code==KeyCode::Back)back(model);
  else if(code==KeyCode::Select)select(model);
  else if(code==KeyCode::Backspace && page==Page::Edit) {if(inputLength)inputValue[--inputLength]=0;}
  else if(code==KeyCode::Up || code==KeyCode::Down)move(code==KeyCode::Up?-1:1,model);
  else if(code==KeyCode::Left || code==KeyCode::Right) {
    const int delta=code==KeyCode::Left?-1:1;
    if(page==Page::Drive)tab=(tab+3+delta)%3;
    else if(page==Page::Recon) {filter=(filter+15+delta)%15;selectedMatch=0;}
    else move(delta,model);
  }
  dirty=true;
}
uint32_t inputLatencyP95Ms() {
  if(!latencyCount)return 0;
  uint32_t sorted[64];std::copy(latencySamples,latencySamples+latencyCount,sorted);
  std::sort(sorted,sorted+latencyCount);
  return sorted[(latencyCount*95+99)/100-1];
}
uint32_t inputLatencyMaxMs() {return inputMaximum;}
uint32_t inputLatencySamples() {return latencyTotal;}
bool allowsBackgroundUpload() {return page!=Page::Edit && page!=Page::Credentials && page!=Page::Uploads && page!=Page::Power;}
void tick(const Model& model) {
  if(!splashShown) {splash();present();splashShown=true;splashAt=millis();return;}
  const uint32_t now=millis();if(now-splashAt<1000)return;
  if(!onboarded) {onboarded=true;if(!model.region_selection_made)openRegions(model,true);}
  if(!dirty && now-paintedAt<500)return;
  if(!dirty && (page==Page::Edit || page==Page::Detail))return;
  render(model);present();dirty=false;paintedAt=now;
}
}
#endif // LORA_TUNNEL_CARDPUTER_ADV
