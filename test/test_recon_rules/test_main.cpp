#include <unity.h>
#include <string>
#include <vector>
#include "recon_rules.h"
using namespace tunnel;
static StaticJsonDocument<4096> document;
void setUp(){document.clear();}
void tearDown(){}
static bool rule(const char* text,Rule& parsed){
  document.clear();return !deserializeJson(document,text)&&parseReconRule(document.as<JsonObjectConst>(),parsed);
}
static ReconPreferences settings(const char* text){
  document.clear();deserializeJson(document,text);return parseReconPreferences(document.as<JsonVariantConst>());
}
void test_defaults_and_strict_optional_settings(){
  auto p=settings("{}");TEST_ASSERT_TRUE(p.buttons_enabled);TEST_ASSERT_TRUE(p.buzzer);
  TEST_ASSERT_EQUAL_UINT8(1,p.display_min);TEST_ASSERT_EQUAL_UINT8(2,p.alert_min);TEST_ASSERT_EQUAL_HEX16(0x7ffe,p.categories);
  p=settings(R"({"ui":{"buttons_enabled":false},"recon":{"buzzer":false,"display_min":"medium","alert_min":"high","categories":["axon","drone"]}})");
  TEST_ASSERT_FALSE(p.buttons_enabled);TEST_ASSERT_FALSE(p.buzzer);TEST_ASSERT_EQUAL_UINT8(2,p.display_min);TEST_ASSERT_EQUAL_UINT8(3,p.alert_min);
  TEST_ASSERT_EQUAL_HEX16((1u<<3)|(1u<<7),p.categories);
  p=settings(R"({"ui":{"buttons_enabled":"false"},"recon":{"buzzer":0,"display_min":3,"alert_min":"high\u0000junk","categories":["axon","bogus"]}})");
  TEST_ASSERT_TRUE(p.buttons_enabled);TEST_ASSERT_TRUE(p.buzzer);TEST_ASSERT_EQUAL_UINT8(1,p.display_min);TEST_ASSERT_EQUAL_UINT8(2,p.alert_min);
  TEST_ASSERT_EQUAL_HEX16(0x7ffe,p.categories);
  TEST_ASSERT_EQUAL_HEX16(0,settings(R"({"recon":{"categories":[]}})").categories);
  TEST_ASSERT_EQUAL_HEX16(0x7ffe,settings(R"({"recon":{"categories":["axon","axon"]}})").categories);
}
void test_legacy_rules_remain_custom_and_low(){
  Rule r;TEST_ASSERT_TRUE(rule(R"({"type":"name","match":"Owned beacon","label":"My beacon","alert":true,"confidence":"high"})",r));
  TEST_ASSERT_EQUAL(int(recon::Category::Custom),int(r.category));TEST_ASSERT_EQUAL(int(recon::Confidence::Low),int(r.confidence));TEST_ASSERT_TRUE(r.alert);
  TEST_ASSERT_TRUE(reconRuleMatches(r,true,"",1,"owned BEACON",nullptr,0));
  TEST_ASSERT_FALSE(reconRuleMatches(r,true,"",1,"Owned beacon suffix",nullptr,0));
}
void test_invalid_rules_do_not_clobber_output(){
  Rule r;strcpy(r.label,"unchanged");
  const char* bad[]={
    R"({"type":"oui","match":"02:13:37"})",R"({"type":"oui","match":"GG:00:00"})",
    R"({"type":"oui","match":"00:25:DFjunk"})",R"({"type":"manufacturer","match":"10000"})",
    R"({"type":"service16","match":"81"})",R"({"type":"name","match":"x","alert":1})",
    R"({"type":"name","match":"x","category":"none"})",R"({"type":"name","match":"x","confidence":"sure"})",
    R"({"type":"name","match":"x\u0000y"})",R"({"type":"name","match":"x","label":"a\nb"})",
    R"({"type":"ssid_prefix","match":"123456789012345678901234567890123"})"
  };
  for(auto json:bad){TEST_ASSERT_FALSE(rule(json,r));TEST_ASSERT_EQUAL_STRING("unchanged",r.label);}
}
void test_oui_rule_ignores_random_and_local_addresses(){
  Rule r;TEST_ASSERT_TRUE(rule(R"({"type":"oui","match":"00:25:DF","category":"axon","confidence":"medium"})",r));
  TEST_ASSERT_TRUE(reconRuleMatches(r,false,"00:25:DF:01:02:03",0,"",nullptr,0));
  TEST_ASSERT_TRUE(reconRuleMatches(r,true,"00:25:df:01:02:03",2,"",nullptr,0));
  TEST_ASSERT_FALSE(reconRuleMatches(r,true,"00:25:DF:01:02:03",1,"",nullptr,0));
  TEST_ASSERT_FALSE(reconRuleMatches(r,true,"00:25:DF:01:02:03",3,"",nullptr,0));
  TEST_ASSERT_FALSE(reconRuleMatches(r,false,"00:25:DFgarbage",0,"",nullptr,0));
}
void test_ssid_prefix_is_wifi_only(){
  Rule r;TEST_ASSERT_TRUE(rule(R"({"type":"ssid_prefix","match":"OWNED-","category":"custom","label":"Owned AP"})",r));
  TEST_ASSERT_TRUE(reconRuleMatches(r,false,"",0,"owned-test",nullptr,0));
  TEST_ASSERT_FALSE(reconRuleMatches(r,true,"",0,"owned-test",nullptr,0));
  TEST_ASSERT_FALSE(reconRuleMatches(r,false,"",0,"own",nullptr,0));
}
void test_manufacturer_rule_validates_entire_ad_stream(){
  Rule r;TEST_ASSERT_TRUE(rule(R"({"type":"manufacturer","match":"0x034D","category":"axon","confidence":"medium"})",r));
  const uint8_t good[]={3,0xff,0x4d,3};const uint8_t broken[]={3,0xff,0x4d,3,9,0xff};
  TEST_ASSERT_TRUE(reconRuleMatches(r,true,"",1,"",good,sizeof(good)));
  TEST_ASSERT_FALSE(reconRuleMatches(r,false,"",0,"",good,sizeof(good)));
  TEST_ASSERT_FALSE(reconRuleMatches(r,true,"",0,"",broken,sizeof(broken)));
  TEST_ASSERT_FALSE(reconRuleMatches(r,true,"",0,"",good,sizeof(good),3));
  const uint8_t split[]={0,3,0xff,0x4d,3};
  TEST_ASSERT_TRUE(reconRuleMatches(r,true,"",0,"",split,sizeof(split),1));
}
void test_service_rule_checks_lists_data_and_exact_base_uuid(){
  Rule r;TEST_ASSERT_TRUE(rule(R"({"type":"service16","match":"FC81","category":"axon"})",r));
  const uint8_t listed[]={3,3,0x81,0xfc};const uint8_t data[]={4,0x16,0x81,0xfc,1};
  TEST_ASSERT_TRUE(reconRuleMatches(r,true,"",0,"",listed,sizeof(listed)));
  TEST_ASSERT_TRUE(reconRuleMatches(r,true,"",0,"",data,sizeof(data)));
  std::vector<uint8_t> custom(18,0);custom[0]=17;custom[1]=7;custom[14]=0x81;custom[15]=0xfc;
  TEST_ASSERT_FALSE(reconRuleMatches(r,true,"",0,"",custom.data(),custom.size()));
  const uint8_t base[]={0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0};memcpy(custom.data()+2,base,12);
  TEST_ASSERT_TRUE(reconRuleMatches(r,true,"",0,"",custom.data(),custom.size()));
  const uint8_t malformed[]={4,3,0x81,0xfc,1};TEST_ASSERT_FALSE(reconRuleMatches(r,true,"",0,"",malformed,sizeof(malformed)));
}
void test_only_stronger_custom_evidence_changes_result_and_alert(){
  Rule r;TEST_ASSERT_TRUE(rule(R"({"type":"name","match":"Test","category":"custom","confidence":"low","label":"My clue","alert":false})",r));
  recon::Result result;auto applied=applyReconRuleSet(result,&r,1,true,"",1,"Test",nullptr,0);
  TEST_ASSERT_TRUE(applied.applied);TEST_ASSERT_FALSE(applied.alert);TEST_ASSERT_EQUAL_STRING("My clue",result.label);
  TEST_ASSERT_NOT_NULL(strstr(result.evidence,"SD user rule"));
  result=recon::classifyWifi("00:25:DF:01:02:03","");
  TEST_ASSERT_EQUAL(int(recon::Confidence::Medium),int(result.confidence));
  applied=applyReconRuleSet(result,&r,1,true,"",1,"Test",nullptr,0);
  TEST_ASSERT_FALSE(applied.applied);TEST_ASSERT_TRUE(applied.alert);TEST_ASSERT_EQUAL(int(recon::Category::Axon),int(result.category));
  r.confidence=recon::Confidence::High;
  applied=applyReconRuleSet(result,&r,1,true,"",1,"Test",nullptr,0);TEST_ASSERT_TRUE(applied.applied);TEST_ASSERT_FALSE(applied.alert);
  strcpy(r.label,"equal should not replace");
  applied=applyReconRuleSet(result,&r,1,true,"",1,"Test",nullptr,0);TEST_ASSERT_FALSE(applied.applied);TEST_ASSERT_EQUAL_STRING("My clue",result.label);
}
void test_rule_count_and_input_lengths_are_bounded(){
  Rule entries[65];for(auto& r:entries){strcpy(r.type,"name");strcpy(r.match,"other");strcpy(r.label,"unused");}
  strcpy(entries[64].match,"Test");recon::Result result;
  TEST_ASSERT_FALSE(applyReconRuleSet(result,entries,65,true,"",1,"Test",nullptr,0).applied);
  const uint8_t tiny=0;TEST_ASSERT_FALSE(reconAdIdentifier(&tiny,recon::kMaxBleBytes+1,0,0,false));
  TEST_ASSERT_FALSE(reconAdIdentifier(nullptr,10,0,0,true));
  Rule parsed;std::string longRule="{\"type\":\"name\",\"match\":\""+std::string(48,'a')+"\"}";
  TEST_ASSERT_FALSE(rule(longRule.c_str(),parsed));
}
int main(int,char**){
  UNITY_BEGIN();
  RUN_TEST(test_defaults_and_strict_optional_settings);
  RUN_TEST(test_legacy_rules_remain_custom_and_low);
  RUN_TEST(test_invalid_rules_do_not_clobber_output);
  RUN_TEST(test_oui_rule_ignores_random_and_local_addresses);
  RUN_TEST(test_ssid_prefix_is_wifi_only);
  RUN_TEST(test_manufacturer_rule_validates_entire_ad_stream);
  RUN_TEST(test_service_rule_checks_lists_data_and_exact_base_uuid);
  RUN_TEST(test_only_stronger_custom_evidence_changes_result_and_alert);
  RUN_TEST(test_rule_count_and_input_lengths_are_bounded);
  return UNITY_END();
}
