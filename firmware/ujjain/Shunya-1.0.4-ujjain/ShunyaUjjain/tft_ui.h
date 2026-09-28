#pragma once
// Port of the supplied Gurgaon renderer: static cards, cached fields and text cells.
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#define C_BG     0x0000
#define C_CARD   0x10A2
#define C_BORDER 0x2945
#define C_TX     0xFFFF
#define C_DIM    0x8C51
#define C_FAINT  0x52AA
#define C_ACCENT 0x3C1E
#define C_ON     0x2DEC
#define C_OFF    0x39C7
#define C_BAD    0xE30D
#define C_AMBER  0xFCC0
#define C_BTN    0x2104
static constexpr int HERO_X=6,HERO_Y=32,HERO_W=228,HERO_H=96;
static constexpr int SEN_Y=134,SEN_H=54,SEN1_X=6,SEN2_X=123,SEN_W=111;
static constexpr int DEV_X=6,DEV_Y=194,DEV_W=228,DEV_H=100,DEV_ROWH=25;
struct UiCache {
  char ip[40],status[32],heroT[20],targetT[20],targetRH[20];
  char sensorT[2][20],sensorRH[2][20],devState[4][24],banner[112];
  int8_t devOn[4];uint16_t linkDot,bannerColor;
} ui;
static void resetUiCache(){
  memset(&ui,0,sizeof(ui));
  // A nonprinting sentinel forces each first draw, including blank banner cells.
  ui.ip[0]=ui.status[0]=ui.heroT[0]=ui.targetT[0]=ui.targetRH[0]=ui.banner[0]=1;
  for(int i=0;i<2;++i)ui.sensorT[i][0]=ui.sensorRH[i][0]=1;
  for(int i=0;i<4;++i){ui.devOn[i]=-1;ui.devState[i][0]=1;}
  ui.linkDot=ui.bannerColor=0xffff;
}
template<size_t N> static bool diff(char (&cache)[N],const char* s){
  if(!strcmp(cache,s))return false;snprintf(cache,N,"%s",s);return true;
}
static void drawTextCell(int x,int y,int w,int h,const char* s,const GFXfont* font,uint16_t fg,uint16_t bg,uint8_t align){
  tft.fillRect(x,y,w,h,bg);tft.setFont(font);tft.setTextSize(1);tft.setTextColor(fg);tft.setTextWrap(false);
  int16_t bx,by;uint16_t bw,bh;tft.getTextBounds(s,0,0,&bx,&by,&bw,&bh);
  if(bw>w-4){tft.setFont();tft.getTextBounds(s,0,0,&bx,&by,&bw,&bh);}
  int tx=align==1?x+(w-(int)bw)/2-bx:align==2?x+w-(int)bw-bx-2:x-bx+2;
  int ty=y+(h-(int)bh)/2-by;tft.setCursor(tx,ty);tft.print(s);tft.setFont();
}
static void drawCard(int x,int y,int w,int h){tft.fillRoundRect(x,y,w,h,10,C_CARD);tft.drawRoundRect(x,y,w,h,10,C_BORDER);}
static void drawTinyLabel(int x,int y,const char* s,uint16_t fg,uint16_t bg){tft.setFont();tft.setTextSize(1);tft.setTextColor(fg,bg);tft.setCursor(x,y);tft.print(s);}
static void drawGfxLeft(int x,int cy,const char* s,const GFXfont* font,uint16_t fg){
  tft.setFont(font);tft.setTextColor(fg);int16_t bx,by;uint16_t bw,bh;tft.getTextBounds(s,0,0,&bx,&by,&bw,&bh);
  tft.setCursor(x-bx,cy-bh/2-by);tft.print(s);tft.setFont();
}
static void clearDisplayFrame(){
  static bool first=true;
  if(first){tft.fillScreen(C_BG);first=false;}
  else tft.fillRect(0,0,240,320,C_BG); // Mode transitions only; never in steady-state passes.
  tft.setTextWrap(false);
}
static void drawStaticDisplayFrame(){
  clearDisplayFrame();
  drawTinyLabel(24,8,FW_VERSION,C_TX,C_BG);tft.drawFastHLine(6,28,228,C_BORDER);
  drawCard(HERO_X,HERO_Y,HERO_W,HERO_H);
  drawTinyLabel(18,44,"AVERAGE C",C_DIM,C_CARD);
  drawTinyLabel(156,67,"TEMP TARGET",C_FAINT,C_CARD);
  drawTinyLabel(156,97,"RH TARGET",C_FAINT,C_CARD);
  drawCard(SEN1_X,SEN_Y,SEN_W,SEN_H);drawCard(SEN2_X,SEN_Y,SEN_W,SEN_H);
  drawTinyLabel(16,SEN_Y+7,"TOP",C_DIM,C_CARD);drawTinyLabel(133,SEN_Y+7,"BOTTOM",C_DIM,C_CARD);
  drawCard(DEV_X,DEV_Y,DEV_W,DEV_H);
  const char* names[]={"Heater","Humidifier","Misting","Exhaust"};
  for(int i=0;i<4;++i){int y=DEV_Y+i*DEV_ROWH;if(i)tft.drawFastHLine(18,y,204,C_BORDER);drawGfxLeft(34,y+DEV_ROWH/2,names[i],&FreeSans9pt7b,C_TX);}
}
static void drawSetupDisplayFrame(){
  clearDisplayFrame();drawTinyLabel(12,10,FW_VERSION,C_DIM,C_BG);
  drawGfxLeft(12,48,"WI-FI SETUP",&FreeSansBold12pt7b,C_ACCENT);
  drawCard(6,72,228,212);
  drawTinyLabel(18,86,"JOIN THIS NETWORK",C_DIM,C_CARD);
  drawTextCell(16,101,206,26,apName,&FreeSansBold9pt7b,C_TX,C_CARD,0);
  drawTinyLabel(18,138,"PASSWORD",C_DIM,C_CARD);
  drawTextCell(16,151,206,24,apPassword,&FreeSans9pt7b,C_TX,C_CARD,0);
  drawTinyLabel(18,187,"OPEN IN YOUR PHONE BROWSER",C_DIM,C_CARD);
  drawTextCell(16,201,206,24,"192.168.4.1",&FreeSansBold12pt7b,C_ACCENT,C_CARD,0);
  drawTinyLabel(18,239,"LOCAL CONTROL CODE",C_DIM,C_CARD);
  drawTextCell(16,251,206,24,apiKey,&FreeSansBold12pt7b,C_TX,C_CARD,0);
}
static uint16_t stateColor(State s){
  switch(s){case State::FAULT:return C_BAD;case State::HEATING:return C_AMBER;case State::HUMIDIFYING:return C_ON;case State::MISTING:return C_AMBER;
  case State::FAE:case State::HIGH_RH_PURGE:case State::PREHEAT_PURGE:return C_BAD;
  case State::POST_HEAT_COOLDOWN:case State::POST_MIST_SETTLE:case State::PREHEAT_SETTLE:case State::OTA_SAFE:return C_ACCENT;default:return C_DIM;}
}
static const char* stateWord(State s){
  switch(s){case State::POST_HEAT_COOLDOWN:return "COOLING";case State::POST_MIST_SETTLE:case State::PREHEAT_SETTLE:return "SETTLING";
  case State::PREHEAT_PURGE:case State::HIGH_RH_PURGE:return "PURGING";case State::FAE:return "VENTING";case State::BOOT_SAFE:return "STARTING";case State::OTA_SAFE:return "UPDATE / RESTART";default:return stateName(s);}
}
// Caller supplies one immutable snapshot. No sensor reads, snapshot() or climate mutation here.
static void drawDisplay(const Snapshot& s){
  static int screen=-1;bool online=WiFi.status()==WL_CONNECTED;int wanted=apActive&&!online?1:0;
  if(screen!=wanted){screen=wanted;if(screen)drawSetupDisplayFrame();else drawStaticDisplayFrame();resetUiCache();}
  const auto& c=s.c;char buf[112];const char* kind;bannerFor(s,buf,sizeof(buf),kind);
  if(!buf[0]){snprintf(buf,sizeof(buf),"CODE %s   %s.local",apiKey,hostName);kind="";}
  uint16_t bc=!strcmp(kind,"fault")?C_BAD:!strcmp(kind,"advisory")?C_AMBER:C_DIM;
  bool changed=diff(ui.banner,buf);if(changed||bc!=ui.bannerColor){
    ui.bannerColor=bc;char line[38];size_t len=std::min<size_t>(strlen(buf),74),split=std::min<size_t>(len,37);
    memcpy(line,buf,split);line[split]=0;drawTextCell(6,299,228,10,line,nullptr,bc,C_BG,0);
    size_t rest=len-split;memcpy(line,buf+split,rest);line[rest]=0;drawTextCell(6,309,228,10,line,nullptr,bc,C_BG,0);
  }
  if(screen)return; // Setup labels are drawn only on mode entry; banner still reflects live safety.
  const StatusView v=statusView(c,s.now);
  uint16_t dot=online?C_ON:apActive?C_AMBER:C_DIM;if(ui.linkDot!=dot){ui.linkDot=dot;tft.fillCircle(12,13,4,dot);}
  if(online)snprintf(buf,sizeof(buf),"%s",WiFi.localIP().toString().c_str());else if(apActive)snprintf(buf,sizeof(buf),"AP %s",apName);else snprintf(buf,sizeof(buf),"no wifi");
  if(diff(ui.ip,buf))drawTextCell(112,4,122,14,buf,nullptr,C_DIM,C_BG,2);
  if(diff(ui.status,stateWord(c.state)))drawTextCell(98,40,130,16,stateWord(c.state),nullptr,stateColor(c.state),C_CARD,2);
  float avg=c.average(s.now);if(std::isfinite(avg))snprintf(buf,sizeof(buf),"%.1f",avg);else snprintf(buf,sizeof(buf),"--");
  if(diff(ui.heroT,buf))drawTextCell(16,63,132,57,buf,&FreeSansBold24pt7b,std::isfinite(avg)?C_TX:C_BAD,C_CARD,0);
  snprintf(buf,sizeof(buf),"%.1f C",c.cfg.temperature_target);if(diff(ui.targetT,buf))drawTextCell(154,78,74,18,buf,&FreeSansBold9pt7b,C_ACCENT,C_CARD,0);
  snprintf(buf,sizeof(buf),"%.0f%%",c.cfg.humidity_target);if(diff(ui.targetRH,buf))drawTextCell(154,107,74,18,buf,&FreeSansBold9pt7b,C_ACCENT,C_CARD,0);
  const Sensor* sensors[]={&c.top,&c.bottom};
  for(int i=0;i<2;++i){int x=i?SEN2_X:SEN1_X;bool ok=sensors[i]->usable(s.now);
    if(ok)snprintf(buf,sizeof(buf),"%.1f C",sensors[i]->t);else snprintf(buf,sizeof(buf),"--");
    if(diff(ui.sensorT[i],buf))drawTextCell(x+10,SEN_Y+16,92,21,buf,&FreeSansBold12pt7b,ok?C_TX:C_BAD,C_CARD,0);
    if(ok)snprintf(buf,sizeof(buf),"%.0f%% RH",sensors[i]->rh);else snprintf(buf,sizeof(buf),"offline");
    if(diff(ui.sensorRH[i],buf))drawTextCell(x+10,SEN_Y+37,92,15,buf,&FreeSans9pt7b,ok?C_ACCENT:C_BAD,C_CARD,0);
  }
  const bool on[]={c.out.heat,c.out.hum,c.out.mist,c.out.exhaust};const Mode modes[]={c.cfg.heater,c.cfg.humidifier,c.cfg.mist,c.cfg.exhaust};
  for(int i=0;i<4;++i){int y=DEV_Y+i*DEV_ROWH;
    if(ui.devOn[i]!=(int)on[i]){ui.devOn[i]=on[i];tft.fillCircle(21,y+DEV_ROWH/2,4,on[i]?C_ON:C_OFF);}
    if(on[i]&&i==2)snprintf(buf,sizeof(buf),"ON %lus",(unsigned long)v.mist_remaining_sec);
    else if(on[i])snprintf(buf,sizeof(buf),"ON");
    else if(modes[i]==Mode::OFF)snprintf(buf,sizeof(buf),"OFF");
    else if(i==0&&v.heat_lock_remaining_sec)snprintf(buf,sizeof(buf),"LOCK %lu:%02lu",(unsigned long)v.heat_lock_remaining_sec/60,(unsigned long)v.heat_lock_remaining_sec%60);
    else if(i==2&&c.mistPending)snprintf(buf,sizeof(buf),"PENDING");
    else if(i==2&&c.cfg.mist_period_min){auto next=remainingSec(s.now,c.mistDueAt,c.cfg.mist_period_min*60000);snprintf(buf,sizeof(buf),"%lu:%02lu",(unsigned long)next/60,(unsigned long)next%60);}
    else snprintf(buf,sizeof(buf),"off");
    if(diff(ui.devState[i],buf))drawTextCell(131,y+1,97,23,buf,&FreeSans9pt7b,on[i]?C_ON:C_DIM,C_CARD,2);
  }
}
