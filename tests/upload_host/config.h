#pragma once
#include <Arduino.h>
namespace tunnel {
struct Config {bool auto_wigle=false,auto_wardrift=false,auto_wdgwars=false;char wifi_name[128]{},wifi_password[128]{},wigle_name[128]{},wigle_token[128]{},wdgwars_token[128]{},wardrift_token[128]{};};
extern Config config;
}
