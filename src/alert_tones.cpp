#include "alert_tones.h"

namespace tunnel { namespace alerts {
namespace {
// Original motifs composed for LORA Tunnel; no game/movie melody transcriptions.
#define CUE(text, rank) {text, uint16_t(sizeof(text)-1), rank}
const Pattern cues[]={
  {nullptr,0,0},
  CUE("robot:d=32,o=6,b=180:c7,g,e7,p,d,a,c7,16f#",90),
  CUE("flock:d=32,o=6,b=180:d7,a,f,p,d7,16c7,a,16f#",90),
  CUE("siren:d=16,o=6,b=200:a5,a,a5,a,32p,a5,a,8a5",85),
  CUE("bodycam:d=16,o=5,b=200:d,a,32p,f,32p,8d6",80),
  CUE("beacon:d=16,o=6,b=200:c,g,32p,8e",35),
  CUE("door:d=16,o=6,b=180:e,c,32p,g5,8c",70),
  CUE("camera:d=16,o=5,b=180:g,b,d6,32p,8b",70),
  CUE("flight:d=32,o=6,b=180:c,e,g,c7,b,g,16e,8g",75),
  CUE("findmy:d=16,o=6,b=200:a,e,32p,b,e,8a",40),
  CUE("tracker:d=16,o=6,b=200:g,d,32p,a,8d",35),
  CUE("smarttag:d=16,o=6,b=200:f,a,32p,c7,8a",35),
  CUE("tile:d=16,o=6,b=200:d,f#,a,32p,8d",35),
  CUE("findhub:d=16,o=6,b=200:c,e,32p,b5,8g",35),
  CUE("glasses:d=32,o=6,b=180:e,g,b,e7,16b,8g",65),
  CUE("acoustic:d=16,o=5,b=180:c,g,c6,32p,8g",65),
  CUE("serial:d=32,o=5,b=180:d,f,a,c6,16a,8d6",60),
  CUE("radio:d=32,o=5,b=180:c,e,g,b,16g,16e6,8c6",55),
  CUE("flipper:d=32,o=5,b=180:e,g,a,e6,16d6,16a,8c6",60),
  CUE("peer:d=32,o=5,b=180:c,d#,g,c6,16a#,16g,8d#6",55),
  CUE("biscuit:d=32,o=5,b=180:g,b,d6,g6,16d6,b,8g",55),
  CUE("custom:d=16,o=5,b=200:c,e,a,32p,8g",45)
};
#undef CUE
static_assert(sizeof(cues)/sizeof(*cues)==size_t(PatternId::Count),"Every cue needs a pattern");
constexpr uint16_t frequencies[4][12]={
 {262,277,294,311,330,349,370,392,415,440,466,494},
 {523,554,587,622,659,698,740,784,831,880,932,988},
 {1047,1109,1175,1245,1319,1397,1480,1568,1661,1760,1865,1976},
 {2093,2217,2349,2489,2637,2794,2960,3136,3322,3520,3729,3951}
};
bool denominator(unsigned value) { return value && value<=32 && !(value&(value-1)); }
bool number(const char* text,uint16_t length,uint16_t& cursor,unsigned& value) {
  const uint16_t start=cursor; value=0;
  while(cursor<length && text[cursor]>='0' && text[cursor]<='9') {
    if(cursor-start>=3) return false;
    value=value*10+unsigned(text[cursor++]-'0');
  }
  return cursor!=start;
}
bool take(const char* text,uint16_t length,uint16_t& cursor,char expected) {
  if(cursor>=length || text[cursor]!=expected) return false;
  ++cursor; return true;
}
bool due(uint32_t now,uint32_t deadline) { return int32_t(now-deadline)>=0; }
char lower(char c) { return c>='A' && c<='Z'?char(c+('a'-'A')):c; }
bool contains(const char* text,size_t capacity,const char* word) {
  // Both classification fields may be unterminated in a corrupt record.
  for(size_t at=0;at<capacity && text[at];++at) {
    size_t i=0;
    while(word[i] && at+i<capacity && text[at+i] && lower(text[at+i])==word[i]) ++i;
    if(!word[i]) return true;
  }
  return false;
}
bool evidence(const recon::Result& result,const char* word) {
  return contains(result.vendor,sizeof(result.vendor),word) || contains(result.evidence,sizeof(result.evidence),word);
}
}

const Pattern& pattern(PatternId id) {
  const auto index=size_t(id); return cues[index<size_t(PatternId::Count)?index:0];
}
PatternId patternFor(const recon::Result& result) {
  using recon::Category;
  switch(result.category) {
    case Category::Alpr:return PatternId::Alpr;
    case Category::Flock:return PatternId::Flock;
    case Category::Axon:return evidence(result,"axon") || evidence(result,"taser")?PatternId::Axon:PatternId::BodyCamera;
    case Category::IBeacon:return PatternId::IBeacon;
    case Category::Ring:return PatternId::Ring;
    case Category::Camera:return PatternId::Camera;
    case Category::Drone:return PatternId::Drone;
    case Category::AirTag:return PatternId::AirTag;
    case Category::Tracker:
      if(evidence(result,"samsung") || evidence(result,"smarttag")) return PatternId::SmartTag;
      if(evidence(result,"tile")) return PatternId::Tile;
      if(evidence(result,"find hub")) return PatternId::FindHub;
      return PatternId::Tracker;
    case Category::Glasses:return PatternId::Glasses;
    case Category::Raven:return PatternId::Raven;
    case Category::Skimmer:return PatternId::Skimmer;
    case Category::Hacker:
      if(evidence(result,"flipper")) return PatternId::Flipper;
      if(evidence(result,"pwnagotchi")) return PatternId::Pwnagotchi;
      if(evidence(result,"biscuit")) return PatternId::Biscuit;
      return PatternId::Hacker;
    case Category::Custom:return PatternId::Custom;
    default:return PatternId::None;
  }
}

bool Reader::begin(const char* text,size_t length) {
  *this=Reader{};
  const auto reject=[this]() { *this=Reader{}; return false; };
  if(!text || !length || length>MAX_PATTERN_BYTES) return reject();
  // Strict ASCII and explicit length: no strlen, reads past end or hidden NUL.
  for(size_t i=0;i<length;++i) if(text[i]<32 || text[i]>126) return reject();
  text_=text; length_=uint16_t(length);
  while(cursor_<length_ && text_[cursor_]!=':') {
    const char c=text_[cursor_];
    if(cursor_>=23 || !((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-')) return reject();
    ++cursor_;
  }
  if(!cursor_ || !take(text_,length_,cursor_,':')) return reject();
  unsigned d=0,o=0,b=0;
  if(!take(text_,length_,cursor_,'d') || !take(text_,length_,cursor_,'=') || !number(text_,length_,cursor_,d) || !denominator(d) ||
     !take(text_,length_,cursor_,',') || !take(text_,length_,cursor_,'o') || !take(text_,length_,cursor_,'=') || !number(text_,length_,cursor_,o) || o<4 || o>7 ||
     !take(text_,length_,cursor_,',') || !take(text_,length_,cursor_,'b') || !take(text_,length_,cursor_,'=') || !number(text_,length_,cursor_,b) || b<60 || b>240 ||
     !take(text_,length_,cursor_,':')) return reject();
  denominator_=uint8_t(d); octave_=uint8_t(o); bpm_=uint16_t(b);
  const uint16_t start=cursor_; unsigned total=0; Note note;
  while(cursor_<length_) {
    if(notes_==MAX_PATTERN_NOTES || !parse(note)) { text_=nullptr; return reject(); }
    total+=note.duration_ms;
    if(total>MAX_PATTERN_MS) { text_=nullptr; return reject(); }
    ++notes_;
  }
  if(!notes_) { text_=nullptr; return reject(); }
  duration_=uint16_t(total); cursor_=start; return true;
}
bool Reader::parse(Note& note) {
  if(!text_ || cursor_>=length_) return false;
  unsigned d=denominator_,o=octave_;
  if(text_[cursor_]>='0' && text_[cursor_]<='9' && (!number(text_,length_,cursor_,d) || !denominator(d))) return false;
  if(cursor_>=length_) return false;
  const char c=text_[cursor_++]; int semitone=-1;
  switch(c) { case 'c':semitone=0;break;case 'd':semitone=2;break;case 'e':semitone=4;break;case 'f':semitone=5;break;case 'g':semitone=7;break;case 'a':semitone=9;break;case 'b':semitone=11;break;case 'p':break;default:return false; }
  if(cursor_<length_ && text_[cursor_]=='#') {
    if(c=='p' || c=='e' || c=='b') return false;
    ++semitone; ++cursor_;
  }
  bool dotted=false;
  if(cursor_<length_ && text_[cursor_]=='.') { dotted=true; ++cursor_; }
  if(cursor_<length_ && text_[cursor_]>='0' && text_[cursor_]<='9') {
    if(c=='p' || !number(text_,length_,cursor_,o) || o<4 || o>7) return false;
  }
  if(cursor_<length_ && text_[cursor_]=='.') {
    if(dotted) return false;
    dotted=true; ++cursor_;
  }
  if(cursor_<length_ && (!take(text_,length_,cursor_,',') || cursor_==length_)) return false;
  unsigned duration=240000u/bpm_/d;
  if(dotted) duration=duration*3u/2u;
  if(!duration || duration>MAX_PATTERN_MS) return false;
  note.frequency_hz=c=='p'?0:frequencies[o-4][semitone];
  note.duration_ms=uint16_t(duration); return true;
}
bool Reader::next(Note& note) {
  if(!text_ || cursor_>=length_) return false;
  return parse(note);
}

void Scheduler::setEnabled(bool enabled) {
  if(!enabled) cancel();
  enabled_=enabled;
}
void Scheduler::cancel() {
  if(active_ && output_) output_(0,0);
  active_=sounding_=false; current_=PatternId::None; reader_=Reader{};
}
bool Scheduler::request(const recon::Result& result,uint32_t now) { return request(patternFor(result),now); }
bool Scheduler::request(PatternId id,uint32_t now) {
  if(!enabled_ || !output_ || id==PatternId::None || size_t(id)>=size_t(PatternId::Count)) return false;
  tick(now);
  const auto& cue=pattern(id);
  if(active_ && cue.priority<=pattern(current_).priority) return false;
  Reader next; Note first;
  if(!next.begin(cue.text,cue.length) || !next.next(first)) return false;
  cancel(); reader_=next; current_=id; active_=true; hardEnd_=now+MAX_PLAYBACK_MS;
  startNote(first,now); return true;
}
void Scheduler::startNote(const Note& note,uint32_t now) {
  noteEnd_=now+note.duration_ms;
  const uint16_t gate=uint16_t(note.duration_ms*85u/100u);
  gateEnd_=now+gate; sounding_=note.frequency_hz!=0;
  output_(note.frequency_hz,sounding_?gate:0);
}
void Scheduler::tick(uint32_t now) {
  if(!active_) return;
  if(due(now,hardEnd_)) { cancel(); return; }
  if(sounding_ && due(now,gateEnd_)) { output_(0,0); sounding_=false; }
  if(!due(now,noteEnd_)) return;
  Note note;
  if(!reader_.next(note)) { cancel(); return; }
  // At most one next note per call; stalled ticks never replay a burst.
  startNote(note,now);
}

uint64_t AlertHistory::fingerprint(uint8_t radioKind,uint8_t addressType,const char* mac) {
  if(!mac || radioKind>1) return 0; // Recon has only Wi-Fi and BLE identities.
  uint64_t hash=14695981039346656037ULL;
  hash=(hash^radioKind)*1099511628211ULL;
  hash=(hash^addressType)*1099511628211ULL;
  for(size_t i=0;i<17;++i) {
    const char c=lower(mac[i]);
    if(!c) return 0;
    if(i%3==2 ? c!=':' : !((c>='0'&&c<='9') || (c>='a'&&c<='f'))) return 0;
    hash=(hash^uint8_t(c))*1099511628211ULL;
  }
  if(mac[17]) return 0;
  return hash?hash:1;
}
HistoryStatus AlertHistory::check(uint8_t kind,uint8_t type,const char* mac) const {
  const uint64_t hash=fingerprint(kind,type,mac);
  if(!hash) return HistoryStatus::Invalid;
  for(const auto value:fingerprints_) {
    if(value==hash) return HistoryStatus::Seen;
    if(!value) return HistoryStatus::Unseen;
  }
  return HistoryStatus::Full;
}
bool AlertHistory::remember(uint8_t kind,uint8_t type,const char* mac) {
  const uint64_t hash=fingerprint(kind,type,mac);
  if(!hash) return false;
  for(auto& value:fingerprints_) {
    if(value==hash) return false;
    if(!value) { value=hash; return true; }
  }
  return false;
}
void AlertHistory::reset() { for(auto& value:fingerprints_) value=0; }
size_t AlertHistory::size() const {
  size_t count=0; while(count<CAPACITY && fingerprints_[count]) ++count; return count;
}
} }
