// Host raster/call recorder for the actual renderer, using Adafruit's real font bitmaps.
#include "../ShunyaUjjain/status_view.h"
#include <Adafruit_GFX.h>
#include <glcdfont.c>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using namespace chamber;
struct FakeTft {
 uint16_t pixels[240*320]={};int calls=0,screenFills=0,fullRects=0,cx=0,cy=0;uint16_t color=0xffff;const GFXfont* f=nullptr;
 bool cell=false;int cellX=0,cellY=0,cellW=240,cellH=320;
 void pixel(int x,int y,uint16_t c){assert(x>=0&&x<240&&y>=0&&y<320);pixels[y*240+x]=c;}
 void fillScreen(uint16_t c){++calls;++screenFills;std::fill(pixels,pixels+240*320,c);}
 void fillRect(int x,int y,int w,int h,uint16_t c){++calls;if(w==240&&h==320)++fullRects;for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)pixel(xx,yy,c);cell=true;cellX=x;cellY=y;cellW=w;cellH=h;}
 bool inside(int x,int y,int w,int h,int r){int xx=x<r?r-1-x:x>=w-r?x-(w-r):0,yy=y<r?r-1-y:y>=h-r?y-(h-r):0;return xx*xx+yy*yy<=r*r;}
 void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){++calls;for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)if(inside(xx,yy,w,h,r))pixel(x+xx,y+yy,c);}
 void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){++calls;for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)if(inside(xx,yy,w,h,r)&&(xx==0||yy==0||xx==w-1||yy==h-1||!inside(xx-1,yy-1,w-2,h-2,r-1)))pixel(x+xx,y+yy,c);}
 void drawFastHLine(int x,int y,int w,uint16_t c){++calls;for(int i=0;i<w;++i)pixel(x+i,y,c);}
 void fillCircle(int x,int y,int r,uint16_t c){++calls;for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)if(xx*xx+yy*yy<=r*r)pixel(x+xx,y+yy,c);}
 void setFont(const GFXfont* v=nullptr){f=v;}void setTextSize(int){}void setTextWrap(bool){}void setTextColor(uint16_t c){color=c;}void setTextColor(uint16_t c,uint16_t){color=c;}void setCursor(int x,int y){cx=x;cy=y;}
 void getTextBounds(const char* s,int x,int y,int16_t* bx,int16_t* by,uint16_t* bw,uint16_t* bh){int minx=999,miny=999,maxx=-999,maxy=-999;for(;*s;++s){unsigned ch=(unsigned char)*s;if(ch=='\n'){x=0;y+=f?f->yAdvance:8;continue;}if(f){if(ch<f->first||ch>f->last)continue;auto g=f->glyph[ch-f->first];if(g.width&&g.height){minx=std::min(minx,x+g.xOffset);maxx=std::max(maxx,x+g.xOffset+g.width-1);miny=std::min(miny,y+g.yOffset);maxy=std::max(maxy,y+g.yOffset+g.height-1);}x+=g.xAdvance;}else{minx=std::min(minx,x);miny=std::min(miny,y);maxx=std::max(maxx,x+5);maxy=std::max(maxy,y+7);x+=6;}}*bx=maxx>=minx?minx:0;*by=maxy>=miny?miny:0;*bw=maxx>=minx?maxx-minx+1:0;*bh=maxy>=miny?maxy-miny+1:0;}
 void print(const char* s){++calls;int16_t bx,by;uint16_t bw,bh;getTextBounds(s,cx,cy,&bx,&by,&bw,&bh);if(cell&&bw&&bh){if(!(bx>=cellX&&by>=cellY&&bx+bw<=cellX+cellW&&by+bh<=cellY+cellH)){std::cerr<<"Text outside cell: "<<s<<" bounds "<<bx<<","<<by<<","<<bw<<","<<bh<<" cell "<<cellX<<","<<cellY<<","<<cellW<<","<<cellH<<"\n";assert(false);}}cell=false;
 for(;*s;++s){unsigned ch=(unsigned char)*s;if(ch=='\n'){cx=0;cy+=f?f->yAdvance:8;continue;}if(f){if(ch<f->first||ch>f->last)continue;auto g=f->glyph[ch-f->first];unsigned bit=0;for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x,++bit)if(f->bitmap[g.bitmapOffset+bit/8]&(0x80>>(bit%8)))pixel(cx+g.xOffset+x,cy+g.yOffset+y,color);cx+=g.xAdvance;}else{for(int x=0;x<5;++x)for(int y=0;y<8;++y)if(font[ch*5+x]&(1<<y))pixel(cx+x,cy+y,color);cx+=6;}}
 }
 void save(const char* name){std::ofstream o(name,std::ios::binary);o<<"P6\n240 320\n255\n";for(auto p:pixels){char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};o.write(rgb,3);}}
} tft;
struct Snapshot{Controller c;uint32_t now=120000;};
constexpr int WL_CONNECTED=3;struct FakeWiFi{bool connected=true;int status(){return connected?3:0;}struct IP{std::string toString(){return "192.168.100.123";}};IP localIP(){return {};}} WiFi;
bool apActive=false;char apName[]="SHUNYA-UJ-238C",apPassword[]="Shunya238C",apiKey[]="1234ABCD",hostName[]="shunya-238c";
static const char* testBanner="";
static void bannerFor(const Snapshot&,char* text,size_t size,const char*& kind){kind=testBanner[0]?"advisory":"";snprintf(text,size,"%s",testBanner);}
#include "../ShunyaUjjain/tft_ui.h"
static void feed(Snapshot& s){for(int i=0;i<3;++i){s.c.top.ingest(true,27,84,s.now);s.c.bottom.ingest(true,26.5,83,s.now);}s.c.tick(s.now);}
int main(int argc,char** argv){
 Snapshot s;feed(s);drawDisplay(s);assert(tft.screenFills==1);int first=tft.calls;
 drawDisplay(s);assert(tft.calls==first); // Identical pass emits no pixel operations.
 s.c.top.t=28;drawDisplay(s);assert(tft.calls-first==4); // Only hero + top temperature cells.
 if(argc>1){std::string p=std::string(argv[1])+"/tft-main.ppm";tft.save(p.c_str());}
 s.c.state=State::POST_HEAT_COOLDOWN;s.c.heatStopped=true;s.c.heatOffAt=s.now-18000;s.c.waterGuardMs=120000;drawDisplay(s);assert(!strcmp(ui.devState[0],"LOCK 1:42"));
 s.c.cfg.heater=Mode::OFF;drawDisplay(s);assert(!strcmp(ui.devState[0],"OFF"));
 s.c.cfg.heater=Mode::AUTO;s.c.heatStopped=false;s.c.state=State::MISTING;s.c.stateAt=s.now-3000;s.c.out.mist=true;drawDisplay(s);assert(!strcmp(ui.devState[2],"ON 7s"));
 for(int i=0;i<3;++i)s.c.bottom.ingest(false,0,0,s.now);drawDisplay(s);assert(!strcmp(ui.sensorRH[1],"offline"));
 if(argc>1){std::string p=std::string(argv[1])+"/tft-degraded.ppm";tft.save(p.c_str());}
 testBanner="SENSOR DEGRADED - HEAT DISABLED";drawDisplay(s);int banner=tft.calls;drawDisplay(s);assert(tft.calls==banner);testBanner="A long notice wraps into the second line of the single banner cell";drawDisplay(s);testBanner="";drawDisplay(s);assert(!strcmp(ui.banner,"CODE 1234ABCD   shunya-238c.local"));
 WiFi.connected=false;apActive=true;drawDisplay(s);int setup=tft.calls;drawDisplay(s);assert(tft.calls==setup);assert(tft.fullRects==1);
 if(argc>1){std::string p=std::string(argv[1])+"/tft-setup.ppm";tft.save(p.c_str());}
 WiFi.connected=true;drawDisplay(s);assert(tft.fullRects==2&&tft.screenFills==1);int restored=tft.calls;drawDisplay(s);assert(tft.calls==restored);
 std::cout<<"PASS: actual TFT renderer, zero redraw on unchanged passes, only changed cells, bounded text with real FreeSans metrics, countdown/mode/offline cells, AP/main transitions, exactly one fillScreen\n";
}
