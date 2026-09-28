#include "../ShunyaUjjain/control.h"
#include <cassert>
#include <iostream>
#include <random>
using namespace chamber;
static void feed(Controller& c,uint32_t n,float top,float bottom,float rh=80){c.top.ingest(true,top,rh,n);c.bottom.ingest(true,bottom,rh,n);}
static void qualify(Controller& c,uint32_t n,float t=24,float b=23,float rh=80){for(int i=0;i<3;++i)feed(c,n,t,b,rh);}
static void step(Controller& c,uint32_t n,float t=24,float b=23,float rh=80){feed(c,n,t,b,rh);c.tick(n);}
static void invariant(const Controller& c,uint32_t n){assert(!(c.out.heat&&(c.out.hum||c.out.mist||c.out.exhaust)));assert(!(c.out.hum&&(c.out.mist||c.out.exhaust)));assert(!(c.out.mist&&c.out.exhaust));if(c.out.heat){assert(c.heatReady(n));assert(!c.hot&&!c.ota&&!c.shutdown&&!c.heatLocked(n));}if(c.out.hum||c.out.mist){assert(c.any(n));assert(!c.hot&&!c.ota&&!c.shutdown&&!c.waterLocked(n));assert(c.high(n)<c.cfg.high_rh);}if(c.hot||c.ota||c.shutdown||!c.any(n))assert(!c.out.heat&&!c.out.hum&&!c.out.mist&&!c.out.exhaust);}
int main(){
 {Config c;assert(validConfig(c));c.temperature_target=NAN;assert(!validConfig(c));c=Config();c.post_heat_sec=0;assert(!validConfig(c));c=Config();c.heater=(Mode)2;assert(!validConfig(c));}
 {Sensor s;for(int i=0;i<2;++i)s.ingest(true,25,80,i);assert(!s.healthy);s.ingest(true,25,80,2);assert(s.usable(2));s.ingest(false,0,0,3);assert(s.healthy&&s.usable(3)&&!s.fresh);s.ingest(false,0,0,4);s.ingest(false,0,0,5);assert(!s.healthy&&std::isnan(s.t));s.ingest(true,25,80,6);s.ingest(true,25,80,7);assert(!s.healthy);s.ingest(true,25,80,8);assert(s.usable(8));assert(!s.usable(3000));}
 {Controller c;qualify(c,1000);c.tick(1000);assert(!c.out.heat);step(c,120000);assert(c.out.heat);step(c,121000,28,26);assert(!c.out.heat&&c.waterLocked(121000));step(c,122000,25,24,30);assert(!c.out.hum&&!c.out.mist);}
 {Controller c;qualify(c,120000);c.tick(120000);assert(c.out.heat);step(c,121000,29,20);assert(!c.out.heat);}
 {Controller c;qualify(c,120000);c.tick(120000);c.top.ingest(false,0,0,120100);c.tick(120100);assert(!c.out.heat&&c.state==State::POST_HEAT_COOLDOWN&&c.waterLocked(120100));}
 {Controller c;qualify(c,120000,26,26,96);c.tick(120000);assert(c.state==State::HIGH_RH_PURGE);step(c,240000,26,26,96);assert(!c.out.exhaust);step(c,540000,26,26,96);assert(c.out.exhaust);step(c,660000,26,26,96);assert(c.purgeIneffective&&!c.out.exhaust);for(uint32_t n=661000;n<=962000;n+=1000)step(c,n,27,27,80);assert(!c.purgeIneffective);}
 {Controller c;qualify(c,120000,24,23,98);c.tick(120000);assert(c.state==State::PREHEAT_PURGE);step(c,240000,24,23,98);assert(c.state==State::PREHEAT_SETTLE&&!c.out.heat);step(c,270000,24,23,98);assert(c.out.heat);}
 {Controller c;qualify(c,120000);c.tick(120000);c.requestMist();step(c,121000);assert(c.mistPending&&!c.out.mist);step(c,122000,28,26);step(c,241999,27,27);assert(!c.out.mist);step(c,242000,27,27);assert(c.out.mist&&!c.mistPending);step(c,252000,24,23);assert(!c.out.mist&&!c.out.heat);step(c,371999,24,23);assert(c.heatPending&&!c.out.heat);step(c,372000,24,23);assert(c.out.heat);}
 {Controller c;qualify(c,120000,27,27,40);c.tick(120000);assert(c.out.hum);step(c,720000,27,27,40);assert(!c.out.hum);step(c,779999,27,27,40);assert(!c.out.hum);step(c,780000,27,27,40);assert(c.out.hum);}
 {Controller c;qualify(c,120000);c.tick(120000);step(c,121000,38,20);assert(c.hot&&!c.out.heat&&!c.out.hum);step(c,122000,35,35);step(c,241999,35,35);assert(c.hot);step(c,242000,35,35);assert(!c.hot);}
 {Controller c;qualify(c,120000);c.tick(120000);c.ota=true;step(c,121000);assert(c.state==State::OTA_SAFE&&!c.out.heat&&!c.safeToReboot(121000));step(c,241000);assert(c.safeToReboot(241000));c.ota=false;step(c,242000);assert(c.out.heat);}
 {Controller c(0xffff0000u);uint32_t n=0xffff0000u+120000u;qualify(c,n);c.tick(n);assert(c.out.heat);step(c,n+1000u,28,26);assert(c.waterLocked(n+119999u));assert(!c.waterLocked(n+121000u));}
 {Controller c;qualify(c,120000,27,27);c.requestMist();c.tick(120000);assert(c.out.mist);for(uint32_t n=120050;n<130000;n+=50){c.requestMist();step(c,n,27,27);}step(c,130000,27,27);assert(!c.out.mist&&!c.mistPending);}
 std::mt19937 rng(7200);Controller c;uint32_t n=0xff000000; c=Controller(n);
 for(int i=0;i<400000;++i){n+=50;bool valid=(rng()%200)!=0;float t=18+(rng()%2300)/100.f,b=18+(rng()%2300)/100.f,rh=30+(rng()%7100)/100.f;c.top.ingest(valid,t,rh,n);c.bottom.ingest((rng()%200)!=0,b,rh,n);if(rng()%2000==0)c.requestMist();if(rng()%3000==0)c.requestHum(n);if(rng()%5000==0)c.requestExhaust();if(rng()%10000==0)c.ota=!c.ota;c.tick(n);invariant(c,n);}
 std::cout<<"PASS: deterministic scenarios and 400,000 randomized safety/timer steps\n";
}
