#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
uint32_t hostMillis=0;
#include "../../src/ui_cardputer.cpp"

namespace {
unsigned assertions=0;
struct Sent {ui::Action action;int field;std::string value;};
std::vector<Sent> sent;
void callback(ui::Action action,int field,const char* value) {sent.push_back({action,field,value});}
void expect(bool ok,const char* message) {++assertions;if(!ok){std::cerr<<"FAIL: "<<message<<"\n";std::exit(1);}}
void key(input::KeyCode code,ui::Model& model,char character=0) {hostMillis+=20;ui::keyboard({code,character},model);ui::tick(model);}
void textKey(char c,ui::Model& model) {key(input::KeyCode::Text,model,c);}
std::string quote(const std::string& text) {std::string out="\"";for(char c:text){if(c=='\\'||c=='\"')out+='\\';out+=c;}return out+'\"';}
void capture(const char* directory,const char* name,ui::Model& model,bool repaint=true) {
  if(repaint) {dirty=true;hw::display().commands.clear();ui::tick(model);}
  std::ofstream out(std::string(directory)+"/"+name+".json",std::ios::binary);out<<"[";bool first=true;
  for(const auto& c:hw::display().commands){if(!first)out<<",";first=false;
    out<<"{\"kind\":"<<quote(c.kind)<<",\"text\":"<<quote(c.text)<<",\"x\":"<<c.x<<",\"y\":"<<c.y<<",\"w\":"<<c.w<<",\"h\":"<<c.h<<",\"r\":"<<c.r<<",\"color\":"<<c.color<<",\"scale\":"<<c.scale<<"}";}
  out<<"]\n";expect(bool(out),"preview command file written");
}
bool screenHas(const char* text) {for(const auto& c:hw::display().commands)if(c.text.find(text)!=std::string::npos)return true;return false;}
}
int main(int argc,char** argv) {
  expect(argc==2,"output directory supplied");const char* output=argv[1];
  ui::Model model;model.sd=true;model.battery=4.04f;model.buttons_enabled=false;
  ui::begin(callback);ui::tick(model);
  capture(output,"splash",model,false);
  expect(buffered,"32.4 KB RGB332 framebuffer allocated");
  expect(hostAllocationBytes==32400,"framebuffer remains exactly 32400 bytes");
  expect(hostAllocationCount==1,"logos require no additional UI heap allocation");
  textKey('w',model);expect(page==Page::Home,"boot splash ignores early input");
  hostMillis=1200;ui::tick(model);expect(page==Page::Regions,"first boot selects region");capture(output,"regions",model);
  key(input::KeyCode::Back,model);expect(page==Page::Home,"Esc skips onboarding");
  expect(sent.back().action==ui::Action::SaveRegion && sent.back().value=="UNSET","skip persists explicit UNSET");
  model.region_selection_made=true;capture(output,"home",model);
  textKey('w',model);expect(page==Page::Drive,"keyboard works with optional buttons disabled");
  textKey(' ',model);expect(sent.back().action==ui::Action::ToggleSession,"Space starts capture");
  model.running=true;model.wifi=324;model.ble=2014;model.lora=17;model.gps=true;strcpy(model.phase,"BLE WINDOW");
  strcpy(model.profile,"US LongFast / 906.875 MHz");
  strcpy(model.recent[0][0],"Example AP  -53 dBm  CH6");strcpy(model.recent[0][1],"Test network  -71 dBm  CH11");
  strcpy(model.recent[1][0],"Example beacon  -60 dBm");strcpy(model.recent[2][0],"?12ab34cd  -93 dBm  7.5 dB");
  capture(output,"wardriver",model);key(input::KeyCode::Left,model);expect(tab==2,"radio tabs wrap backward");
  capture(output,"lora",model);key(input::KeyCode::Back,model);expect(model.running,"Home does not stop capture");
  textKey('r',model);expect(sent.back().action==ui::Action::ToggleMode && sent.back().field==1,"Recon requests mode transition");
  model.recon=true;model.recon_total=6;
  strcpy(model.recon_recent[0],"Example Find My advertisement");strcpy(model.recon_evidence[0],"Matching broadcast format; identity is not established.");model.recon_categories[0]=8;model.recon_confidence[0]=2;
  strcpy(model.recon_recent[1],"Example Remote ID broadcast");strcpy(model.recon_evidence[1],"Unauthenticated self-reported aircraft identification.");model.recon_categories[1]=7;model.recon_confidence[1]=3;
  capture(output,"recon",model);key(input::KeyCode::Select,model);expect(page==Page::Detail,"Enter opens evidence");capture(output,"detail",model);
  key(input::KeyCode::Back,model);textKey('m',model);expect(sent.back().action==ui::Action::ToggleAlerts && sent.back().field==0,"M toggles alerts");
  key(input::KeyCode::Right,model);expect(filter==1,"Right changes category");key(input::KeyCode::Select,model);expect(page==Page::Recon,"empty filter has no fake detail");
  textKey('s',model);expect(page==Page::Settings,"S opens settings during capture");capture(output,"settings",model);
  focus=4;capture(output,"settings-more",model);focus=0;
  key(input::KeyCode::Select,model);expect(page==Page::Regions,"region settings reachable");
  const size_t before=sent.size();key(input::KeyCode::Select,model);expect(sent.size()==before,"running capture prevents region change");
  model.running=false;
  for(size_t i=0;i<tunnel::lora_profiles::profileCount;++i) {
    regionIndex=int(i);show(Page::Regions);const size_t count=sent.size();key(input::KeyCode::Select,model);
    expect((sent.size()!=count)==tunnel::lora_profiles::selectable(tunnel::lora_profiles::profiles[i]),"only board-compatible regions can save");
  }
  show(Page::Settings);key(input::KeyCode::Down,model);key(input::KeyCode::Select,model);expect(page==Page::Credentials,"credentials reachable");
  capture(output,"credentials",model);focus=4;capture(output,"credentials-more",model);focus=0;key(input::KeyCode::Down,model);key(input::KeyCode::Select,model);
  expect(page==Page::Edit && editField==1,"WiGLE token edit selected");
  const char* secret="TestToken!@#$%: /,.;`+_=";for(const char* p=secret;*p;++p)textKey(*p,model);
  expect(inputLength==strlen(secret),"punctuation and space type without navigation");
  capture(output,"keyboard",model);expect(!screenHas("TestToken"),"secret input is masked");
  key(input::KeyCode::Backspace,model);expect(inputLength==strlen(secret)-1,"Backspace deletes one character");
  key(input::KeyCode::Select,model);expect(sent.back().action==ui::Action::SaveCredential && sent.back().field==1,"Enter saves correct credential");
  expect(inputLength==0 && inputValue[0]==0,"saved input wiped");
  key(input::KeyCode::Select,model);for(int i=0;i<150;++i)textKey('A',model);expect(inputLength==127,"input capacity bounded");
  const size_t oldCount=sent.size();key(input::KeyCode::Back,model);expect(sent.size()==oldCount && inputLength==0,"cancel wipes without save");
  for(unsigned fieldIndex=0;fieldIndex<6;++fieldIndex) {
    show(Page::Credentials);focus=fieldIndex;key(input::KeyCode::Select,model);
    const char* value=fieldIndex==4?"testpass":"x";
    for(const char* p=value;*p;++p)textKey(*p,model);key(input::KeyCode::Select,model);
    expect(sent.back().field==int(fieldIndex) && sent.back().value==value,"all credential fields reachable");
  }
  show(Page::Credentials);focus=4;key(input::KeyCode::Select,model);textKey('x',model);
  expect(!ui::allowsBackgroundUpload(),"editing prevents automatic association");
  const size_t invalidCount=sent.size();key(input::KeyCode::Select,model);
  expect(page==Page::Edit && inputLength==1 && sent.size()==invalidCount,"invalid password remains editable without save");
  capture(output,"keyboard-invalid",model);
  show(Page::Settings);focus=2;key(input::KeyCode::Select,model);expect(page==Page::Uploads,"uploads reachable");capture(output,"uploads",model);
  focus=4;capture(output,"uploads-more",model);focus=0;
  key(input::KeyCode::Select,model);expect(sent.back().action==ui::Action::ToggleAutoUpload && sent.back().field==0 && sent.back().value=="1","enable service explicit");
  model.auto_wigle=true;key(input::KeyCode::Select,model);expect(sent.back().value=="0","disable service explicit");
  model.running=true;focus=3;const size_t uploadCount=sent.size();key(input::KeyCode::Select,model);expect(sent.size()==uploadCount,"manual upload blocked during capture");expect(screenHas("Stop and save before upload"),"blocked manual upload has visible explanation");
  model.running=false;key(input::KeyCode::Select,model);expect(sent.back().action==ui::Action::UploadNow && sent.back().field==0,"manual WiGLE upload requested");
  model.upload_busy=true;focus=6;key(input::KeyCode::Select,model);expect(sent.back().action==ui::Action::CancelUpload,"upload cancellation reachable");model.upload_busy=false;
  show(Page::Settings);focus=5;key(input::KeyCode::Select,model);expect(page==Page::Power && focus==0,"Power defaults to Cancel");capture(output,"power",model);
  const size_t powerCount=sent.size();key(input::KeyCode::Select,model);expect(page==Page::Settings && sent.size()==powerCount,"Power Cancel emits no shutdown");
  focus=5;key(input::KeyCode::Select,model);key(input::KeyCode::Right,model);key(input::KeyCode::Select,model);expect(sent.back().action==ui::Action::Shutdown,"Power confirmation requests shutdown");
  model.shutting_down=true;const size_t shuttingCount=sent.size();key(input::KeyCode::Select,model);textKey('w',model);expect(sent.size()==shuttingCount,"shutdown ignores input");capture(output,"saving",model);
  model.shutting_down=false;show(Page::Uploads);
  std::strcpy(model.upload_status,"WDGWars: job queued; receipt pending");
  std::strcpy(model.notice,"Upload preference save failed");
  hw::display().commands.clear();dirty=true;ui::tick(model);
  expect(screenHas("WDGWars: job queued"),"Uploads retains current worker status");
  expect(screenHas("Upload preference save failed"),"Uploads also shows setting/refusal notice");
  bool statusLine=false,noticeLine=false;
  for(const auto& command:hw::display().commands){
    if(command.text.find("WDGWars: job queued")!=std::string::npos)statusLine=command.y==98;
    if(command.text.find("Upload preference save failed")!=std::string::npos)noticeLine=command.y==110;}
  expect(statusLine&&noticeLine,"status and action notice occupy distinct nonoverlapping rows");capture(output,"uploads-status-notice",model);
  expect(!ui::allowsBackgroundUpload()&&ui::allowsUploadJobPolling(),"Uploads allows existing-job polling but suppresses new automatic uploads");
  for(Page guarded:{Page::Credentials,Page::Edit,Page::Power}){show(guarded);expect(!ui::allowsBackgroundUpload()&&!ui::allowsUploadJobPolling(),"editing and Power suppress both network scheduling paths");}
  show(Page::Settings);expect(ui::allowsBackgroundUpload()&&ui::allowsUploadJobPolling(),"normal idle page allows automatic work and job continuation");
  show(Page::Uploads);focus=6;hw::display().commands.clear();dirty=true;ui::tick(model);
  expect(screenHas("Cancel upload")&&screenHas("3/3"),"Cancel remains reachable on third Uploads page");
  key(input::KeyCode::Select,model);expect(sent.back().action==ui::Action::CancelUpload,"third-page cancel action preserved");
  // Every page/focus and long valid model string must remain within 240 x 135.
  model.shutting_down=false;memset(model.notice,'N',sizeof(model.notice)-1);memset(model.upload_status,'U',sizeof(model.upload_status)-1);
  for(Page next:{Page::Home,Page::Drive,Page::Recon,Page::Detail,Page::Settings,Page::Credentials,Page::Edit,Page::Regions,Page::Power,Page::Uploads}) {
    show(next);const unsigned count=std::max(1U,menuCount());
    for(unsigned item=0;item<count;++item){focus=item;dirty=true;ui::tick(model);++assertions;}
  }
  show(Page::Home);ui::keyboard({input::KeyCode::Down,0},model);hostMillis+=45;ui::tick(model);
  expect(ui::inputLatencyMaxMs()>=45,"input latency includes queued time before draw");
  expect(latencyCount==64,"latency sample storage stays bounded after many key events");
  expect(ui::inputLatencyP95Ms()<=ui::inputLatencyMaxMs(),"latency percentile bounded by maximum");
  std::cout<<"Cardputer UI: "<<assertions<<" checks passed; all draw bounds checked. Synthetic previews only.\n";
}
