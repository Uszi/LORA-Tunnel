#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lgfx {
namespace fonts { constexpr int Font0=0; }
namespace textdatum_t { constexpr int top_left=0; }
struct Command { std::string kind,text; int x=0,y=0,w=0,h=0,r=0,color=0,scale=1; };
class LovyanGFX {
public:
  int width=240,height=135,scale=1,ink=65535;
  std::vector<Command> commands;
  void check(int x,int y,int w,int h) const { assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=width && y+h<=height); }
  void fillScreen(int color) { commands.clear();fillRect(0,0,width,height,color); }
  void fillRect(int x,int y,int w,int h,int color) { check(x,y,w,h);commands.push_back({"rect","",x,y,w,h,0,color,1}); }
  void fillRoundRect(int x,int y,int w,int h,int radius,int color) { check(x,y,w,h);commands.push_back({"round","",x,y,w,h,radius,color,1}); }
  void drawFastHLine(int x,int y,int w,int color) { fillRect(x,y,w,1,color); }
  void setTextSize(int value) { scale=value; }
  void setTextColor(int value) { ink=value; }
  void setTextDatum(int) {}
  void setTextWrap(bool,bool) {}
  void setFont(const int*) {}
  void drawString(const char* text,int x,int y) { const int w=int(std::strlen(text))*6*scale;check(x,y,w,8*scale);commands.push_back({"text",text,x,y,w,8*scale,0,ink,scale}); }

};
class LGFX_Device:public LovyanGFX {};
class LGFX_Sprite:public LovyanGFX {
public:
  void setColorDepth(int) {}
  void setPsram(bool) {}
  void setBuffer(void*,int w,int h,int) {width=w;height=h;}
  void* createSprite(int w,int h) {width=w;height=h;return this;}
  void pushSprite(LovyanGFX* target,int x,int y) {
    for(auto command:commands) { command.x+=x;command.y+=y;target->check(command.x,command.y,command.w,command.h);target->commands.push_back(command); }
  }
};
}
namespace hw { inline lgfx::LGFX_Device& display() {static lgfx::LGFX_Device screen;return screen;} }
