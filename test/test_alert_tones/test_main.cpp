#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <initializer_list>
#include <unity.h>
#include "alert_tones.h"
// Native excludes src/. Exercise the exact production translation unit.
#include "../../src/alert_tones.cpp"
using namespace tunnel;
using namespace tunnel::alerts;
namespace {
struct Pulse { uint16_t hz,duration; };
Pulse pulses[512]; size_t pulseCount=0,allocations=0;
void sink(uint16_t hz,uint16_t duration) { if(pulseCount<512) pulses[pulseCount++]={hz,duration}; }
size_t sounds() { size_t n=0; for(size_t i=0;i<pulseCount;++i) n+=pulses[i].hz!=0; return n; }
bool startOnce(Scheduler& player,AlertHistory& history,uint8_t kind,uint8_t type,const char* id,PatternId cue,uint32_t now) {
  if(history.check(kind,type,id)!=HistoryStatus::Unseen || !player.request(cue,now)) return false;
  return history.remember(kind,type,id);
}
}
void* operator new(size_t n) { ++allocations; if(void* p=std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
void operator delete[](void* p,size_t) noexcept { std::free(p); }
void setUp() { pulseCount=0; }
void tearDown() {}

void test_all_original_patterns_are_short_bounded_and_distinct() {
  for(unsigned id=1;id<unsigned(PatternId::Count);++id) {
    const auto& cue=pattern(PatternId(id)); Reader reader; Note note;
    TEST_ASSERT_TRUE(reader.begin(cue.text,cue.length));
    TEST_ASSERT_TRUE(reader.durationMs()<=MAX_PATTERN_MS);
    TEST_ASSERT_TRUE(reader.noteCount()>=3 && reader.noteCount()<=MAX_PATTERN_NOTES);
    unsigned count=0,total=0,nonzero=0;
    while(reader.next(note)) {
      TEST_ASSERT_TRUE(note.duration_ms>0); TEST_ASSERT_TRUE(note.frequency_hz<=3951);
      total+=note.duration_ms; nonzero+=note.frequency_hz!=0; ++count;
    }
    TEST_ASSERT_EQUAL_UINT(reader.noteCount(),count);
    TEST_ASSERT_EQUAL_UINT(reader.durationMs(),total);
    TEST_ASSERT_TRUE(nonzero>=3);
    TEST_ASSERT_FALSE(reader.next(note));
    for(unsigned other=1;other<id;++other) TEST_ASSERT_NOT_EQUAL(0,std::strcmp(cue.text,pattern(PatternId(other)).text));
  }
  TEST_ASSERT_NULL(pattern(PatternId::None).text);
  TEST_ASSERT_NULL(pattern(PatternId(255)).text);
}
void test_reader_exact_notes_sharps_dots_pauses_and_explicit_lengths() {
  const char text[]="t:d=16,o=5,b=120:32c#6.,16p,8g5"; Reader r; Note n;
  TEST_ASSERT_TRUE(r.begin(text,sizeof(text)-1));
  TEST_ASSERT_TRUE(r.next(n)); TEST_ASSERT_EQUAL_UINT16(1109,n.frequency_hz); TEST_ASSERT_EQUAL_UINT16(93,n.duration_ms);
  TEST_ASSERT_TRUE(r.next(n)); TEST_ASSERT_EQUAL_UINT16(0,n.frequency_hz); TEST_ASSERT_EQUAL_UINT16(125,n.duration_ms);
  TEST_ASSERT_TRUE(r.next(n)); TEST_ASSERT_EQUAL_UINT16(784,n.frequency_hz); TEST_ASSERT_EQUAL_UINT16(250,n.duration_ms);
  TEST_ASSERT_FALSE(r.next(n));
  const char before[]="t:d=16,o=5,b=120:c.6";
  TEST_ASSERT_TRUE(r.begin(before,sizeof(before)-1)); TEST_ASSERT_TRUE(r.next(n));
  TEST_ASSERT_EQUAL_UINT16(1047,n.frequency_hz); TEST_ASSERT_EQUAL_UINT16(187,n.duration_ms);
  // A bounded non-NUL-terminated input is valid; the following byte is ignored.
  char bounded[]={'x',':','d','=','1','6',',','o','=','5',',','b','=','2','0','0',':','c','!'};
  TEST_ASSERT_TRUE(r.begin(bounded,sizeof(bounded)-1));
}
void test_malformed_reader_never_plays_a_valid_prefix_or_divides_by_zero() {
  const char* invalid[]={"",":d=16,o=5,b=120:c","x:d=3,o=5,b=120:c","x:d=0,o=5,b=120:c",
    "x:d=16,o=3,b=120:c","x:d=16,o=8,b=120:c","x:d=16,o=5,b=0:c","x:d=16,o=5,b=241:c",
    "x:d=16,o=5,b=120:","x:d=16,o=5,b=120:c,","x:d=16,o=5,b=120:c,,d",
    "x:d=16,o=5,b=120:c,garbage","x:d=16,o=5,b=120:0c","x:d=16,o=5,b=120:9999c",
    "x:d=16,o=5,b=120:c8","x:d=16,o=5,b=120:p#","x:d=16,o=5,b=120:p6",
    "x:d=16,o=5,b=120:e#","x:d=16,o=5,b=120:b#","x:d=16,o=5,b=120:c..",
    "x:d=16,o=5,b=120:c.6.","x:d=16,o=5,b=120:c\n","x:d=1,o=5,b=60:c",
    "x:d=16,o=5,b=120:c,c,c,c,c,c,c,c,c","x:d=32,o=5,b=240:c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c,c"};
  Reader r; Note n;
  for(const auto text:invalid) {
    TEST_ASSERT_FALSE_MESSAGE(r.begin(text,std::strlen(text)),text);
    TEST_ASSERT_FALSE(r.next(n)); TEST_ASSERT_EQUAL_UINT(0,r.noteCount());
  }
  TEST_ASSERT_FALSE(r.begin(nullptr,2)); TEST_ASSERT_FALSE(r.next(n));
  TEST_ASSERT_FALSE(r.begin("x",MAX_PATTERN_BYTES+1)); TEST_ASSERT_FALSE(r.next(n));
  const char embedded[]="x:d=16,o=5,b=120:c\0,d";
  TEST_ASSERT_FALSE(r.begin(embedded,sizeof(embedded)-1)); TEST_ASSERT_FALSE(r.next(n));
}
void test_mapping_covers_categories_and_uses_only_supported_classification_evidence() {
  recon::Result r;
  for(unsigned category=1;category<=14;++category) { r.category=recon::Category(category); TEST_ASSERT_NOT_EQUAL(unsigned(PatternId::None),unsigned(patternFor(r))); }
  r.category=recon::Category::Axon;
  TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::BodyCamera),unsigned(patternFor(r)));
  std::strcpy(r.evidence,"Axon/TASER assigned service; model unknown");
  TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::Axon),unsigned(patternFor(r)));
  r={}; r.category=recon::Category::Tracker;
  std::strcpy(r.vendor,"Samsung"); TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::SmartTag),unsigned(patternFor(r)));
  std::strcpy(r.vendor,"Tile"); TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::Tile),unsigned(patternFor(r)));
  std::strcpy(r.vendor,"Find Hub format"); TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::FindHub),unsigned(patternFor(r)));
  r={}; r.category=recon::Category::Hacker;
  std::strcpy(r.evidence,"Flipper Zero service"); TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::Flipper),unsigned(patternFor(r)));
  std::strcpy(r.evidence,"Pwnagotchi peer fields"); TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::Pwnagotchi),unsigned(patternFor(r)));
  std::strcpy(r.evidence,"Biscuit name + service"); TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::Biscuit),unsigned(patternFor(r)));
  r={}; r.category=recon::Category::Camera; std::strcpy(r.evidence,"Axon arbitrary text");
  TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::Camera),unsigned(patternFor(r)));
  r={}; r.category=recon::Category::Axon; std::memset(r.vendor,'z',sizeof(r.vendor)); std::memset(r.evidence,'z',sizeof(r.evidence));
  TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::BodyCamera),unsigned(patternFor(r)));
  r.category=recon::Category(255); TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::None),unsigned(patternFor(r)));
}
void test_disabled_scheduler_and_missing_sink_cannot_start() {
  Scheduler player(sink);
  TEST_ASSERT_FALSE(player.request(PatternId::Axon,0)); TEST_ASSERT_EQUAL_UINT(0,pulseCount);
  player.setEnabled(true); TEST_ASSERT_FALSE(player.request(PatternId::None,0));
  TEST_ASSERT_FALSE(player.request(PatternId(255),0)); TEST_ASSERT_EQUAL_UINT(0,pulseCount);
  Scheduler silent(nullptr); silent.setEnabled(true); TEST_ASSERT_FALSE(silent.request(PatternId::Axon,0));
}
void test_each_motif_finishes_once_and_never_loops() {
  for(unsigned id=1;id<unsigned(PatternId::Count);++id) {
    pulseCount=0; Scheduler player(sink); player.setEnabled(true);
    TEST_ASSERT_TRUE(player.request(PatternId(id),100));
    for(uint32_t now=101;now<=1300;++now) player.tick(now);
    TEST_ASSERT_FALSE(player.active());
    const size_t emitted=pulseCount,notes=sounds(); TEST_ASSERT_TRUE(notes>=3);
    TEST_ASSERT_EQUAL_UINT16(0,pulses[pulseCount-1].hz);
    player.tick(20000); player.tick(30000);
    TEST_ASSERT_EQUAL_UINT(emitted,pulseCount);
    for(size_t i=0;i<pulseCount;++i) if(pulses[i].hz) TEST_ASSERT_TRUE(pulses[i].duration>0 && pulses[i].duration<=MAX_PATTERN_MS);
  }
}
void test_priority_preemption_has_no_queue_or_replay() {
  Scheduler player(sink); player.setEnabled(true);
  TEST_ASSERT_TRUE(player.request(PatternId::Tracker,10));
  const auto before=pulseCount;
  TEST_ASSERT_FALSE(player.request(PatternId::Tracker,11)); TEST_ASSERT_EQUAL_UINT(before,pulseCount);
  TEST_ASSERT_TRUE(player.request(PatternId::Axon,12));
  TEST_ASSERT_EQUAL_UINT16(0,pulses[before].hz);
  TEST_ASSERT_EQUAL_UINT8(unsigned(PatternId::Axon),unsigned(player.current()));
  TEST_ASSERT_FALSE(player.request(PatternId::IBeacon,13));
  for(uint32_t now=14;now<1500;++now) player.tick(now);
  const auto after=pulseCount; player.tick(10000); TEST_ASSERT_EQUAL_UINT(after,pulseCount);
}
void test_mute_cancel_and_unmute_do_not_resume_a_motif() {
  Scheduler player(sink); player.setEnabled(true); TEST_ASSERT_TRUE(player.request(PatternId::Alpr,0));
  player.setEnabled(false); TEST_ASSERT_FALSE(player.active()); TEST_ASSERT_EQUAL_UINT16(0,pulses[pulseCount-1].hz);
  const auto before=pulseCount; player.tick(200); player.setEnabled(true); player.tick(1000);
  TEST_ASSERT_EQUAL_UINT(before,pulseCount);
  TEST_ASSERT_TRUE(player.request(PatternId::Ring,1001)); player.cancel();
  TEST_ASSERT_FALSE(player.active()); TEST_ASSERT_EQUAL_UINT16(0,pulses[pulseCount-1].hz);
}
void test_deadlines_survive_wrap_and_stalled_ticks_never_burst() {
  Scheduler player(sink); player.setEnabled(true);
  const uint32_t start=UINT32_MAX-50;
  TEST_ASSERT_TRUE(player.request(PatternId::Axon,start));
  for(uint32_t elapsed=1;elapsed<=MAX_PLAYBACK_MS;++elapsed) player.tick(start+elapsed);
  TEST_ASSERT_FALSE(player.active());
  TEST_ASSERT_TRUE(player.request(PatternId::Alpr,2000)); const auto before=pulseCount;
  player.tick(3100); TEST_ASSERT_TRUE(pulseCount<=before+2); // one silence + one next note
  player.tick(3200); TEST_ASSERT_FALSE(player.active());
  const auto stopped=pulseCount; player.tick(100000); TEST_ASSERT_EQUAL_UINT(stopped,pulseCount);
}
void test_history_normalizes_address_but_distinguishes_radio_and_address_type() {
  AlertHistory history; TEST_ASSERT_EQUAL_UINT(1024,sizeof(history));
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Unseen),unsigned(history.check(0,255,"AA:BB:CC:DD:EE:FF")));
  TEST_ASSERT_TRUE(history.remember(0,255,"AA:BB:CC:DD:EE:FF"));
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Seen),unsigned(history.check(0,255,"aa:bb:cc:dd:ee:ff")));
  TEST_ASSERT_FALSE(history.remember(0,255,"aa:bb:cc:dd:ee:ff"));
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Unseen),unsigned(history.check(1,255,"aa:bb:cc:dd:ee:ff")));
  TEST_ASSERT_TRUE(history.remember(1,0,"aa:bb:cc:dd:ee:ff"));
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Unseen),unsigned(history.check(1,1,"aa:bb:cc:dd:ee:ff")));
  TEST_ASSERT_EQUAL_UINT(2,history.size());
  for(const auto id:{"","01:02","01:02:03:04:05:zz","01-02-03-04-05-06","01:02:03:04:05:06x"}) {
    TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Invalid),unsigned(history.check(0,0,id))); TEST_ASSERT_FALSE(history.remember(0,0,id));
  }
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Invalid),unsigned(history.check(2,0,"01:02:03:04:05:06")));
  TEST_ASSERT_FALSE(history.remember(0,0,nullptr));
}
void test_history_full_suppresses_new_audio_without_eviction_or_repeat() {
  AlertHistory history; char id[18];
  for(unsigned i=0;i<AlertHistory::CAPACITY;++i) { std::snprintf(id,sizeof(id),"02:00:00:00:00:%02x",i); TEST_ASSERT_TRUE(history.remember(1,1,id)); }
  TEST_ASSERT_TRUE(history.full()); TEST_ASSERT_EQUAL_UINT(128,history.size());
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Seen),unsigned(history.check(1,1,"02:00:00:00:00:00")));
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Full),unsigned(history.check(1,1,"02:00:00:00:00:ff")));
  TEST_ASSERT_FALSE(history.remember(1,1,"02:00:00:00:00:ff"));
  TEST_ASSERT_EQUAL_UINT8(unsigned(HistoryStatus::Seen),unsigned(history.check(1,1,"02:00:00:00:00:00")));
}
void test_per_session_history_survives_mute_and_display_cache_churn() {
  Scheduler player(sink); AlertHistory history; player.setEnabled(true);
  const char* original="02:01:02:03:04:05";
  TEST_ASSERT_TRUE(startOnce(player,history,1,1,original,PatternId::Tracker,0));
  player.setEnabled(false); player.setEnabled(true);
  TEST_ASSERT_FALSE(startOnce(player,history,1,1,original,PatternId::Tracker,2000));
  // 100 other identities exceed the firmware's 64-slot display summary cache.
  char id[18]; for(unsigned i=0;i<100;++i) { std::snprintf(id,sizeof(id),"02:00:00:00:01:%02x",i); TEST_ASSERT_TRUE(history.remember(1,1,id)); }
  TEST_ASSERT_FALSE(startOnce(player,history,1,1,original,PatternId::Tracker,60000));
  history.reset(); TEST_ASSERT_FALSE(history.full()); TEST_ASSERT_EQUAL_UINT(0,history.size());
  TEST_ASSERT_TRUE(startOnce(player,history,1,1,original,PatternId::Tracker,60001));
}
void test_busy_rejection_does_not_consume_new_identity_and_preempted_identity_stays_seen() {
  Scheduler player(sink); AlertHistory history; player.setEnabled(true);
  TEST_ASSERT_TRUE(startOnce(player,history,0,255,"02:00:00:00:00:01",PatternId::Tracker,0));
  TEST_ASSERT_FALSE(startOnce(player,history,0,255,"02:00:00:00:00:02",PatternId::Tracker,1));
  TEST_ASSERT_EQUAL_UINT(1,history.size());
  TEST_ASSERT_TRUE(startOnce(player,history,0,255,"02:00:00:00:00:03",PatternId::Alpr,2));
  TEST_ASSERT_FALSE(startOnce(player,history,0,255,"02:00:00:00:00:01",PatternId::Axon,2000));
  TEST_ASSERT_TRUE(startOnce(player,history,0,255,"02:00:00:00:00:02",PatternId::Tracker,2001));
  TEST_ASSERT_EQUAL_UINT(3,history.size());
}
void test_runtime_reader_scheduler_mapping_and_history_do_not_allocate() {
  Scheduler player(sink); AlertHistory history; recon::Result r; r.category=recon::Category::Axon;
  std::strcpy(r.evidence,"Axon assigned service"); const auto before=allocations;
  player.setEnabled(true); const auto id=patternFor(r);
  const bool admitted=startOnce(player,history,1,0,"02:00:00:00:00:01",id,0);
  for(uint32_t now=1;now<1500;++now) player.tick(now);
  player.setEnabled(false); history.check(1,0,"02:00:00:00:00:01"); history.full(); history.size(); history.reset();
  TEST_ASSERT_TRUE(admitted); TEST_ASSERT_EQUAL_UINT(before,allocations);
}
int main(int,char**) {
 UNITY_BEGIN();
 RUN_TEST(test_all_original_patterns_are_short_bounded_and_distinct);
 RUN_TEST(test_reader_exact_notes_sharps_dots_pauses_and_explicit_lengths);
 RUN_TEST(test_malformed_reader_never_plays_a_valid_prefix_or_divides_by_zero);
 RUN_TEST(test_mapping_covers_categories_and_uses_only_supported_classification_evidence);
 RUN_TEST(test_disabled_scheduler_and_missing_sink_cannot_start);
 RUN_TEST(test_each_motif_finishes_once_and_never_loops);
 RUN_TEST(test_priority_preemption_has_no_queue_or_replay);
 RUN_TEST(test_mute_cancel_and_unmute_do_not_resume_a_motif);
 RUN_TEST(test_deadlines_survive_wrap_and_stalled_ticks_never_burst);
 RUN_TEST(test_history_normalizes_address_but_distinguishes_radio_and_address_type);
 RUN_TEST(test_history_full_suppresses_new_audio_without_eviction_or_repeat);
 RUN_TEST(test_per_session_history_survives_mute_and_display_cache_churn);
 RUN_TEST(test_busy_rejection_does_not_consume_new_identity_and_preempted_identity_stays_seen);
 RUN_TEST(test_runtime_reader_scheduler_mapping_and_history_do_not_allocate);
 return UNITY_END();
}
