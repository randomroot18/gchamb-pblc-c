#include "../ShunyaUjjain/control.h"
#include "../ShunyaUjjain/config_migration.h"
#include "../ShunyaUjjain/device_identity.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace chamber;
static void feed(Controller& c,uint32_t n,float t=27,float b=27,float tr=84,float br=84){
 c.top.ingest(true,t,tr,n,c.cfg.top_t_offset,c.cfg.top_rh_offset);
 c.bottom.ingest(true,b,br,n,c.cfg.bottom_t_offset,c.cfg.bottom_rh_offset);
}
static void ready(Controller& c,uint32_t n=0,float t=27,float b=27,float tr=84,float br=84){for(int i=0;i<3;++i)feed(c,n,t,b,tr,br);}
static void safe(const Controller& c,uint32_t n){
 assert(!(c.out.heat&&(c.out.hum||c.out.mist||c.out.exhaust)));
 assert(!(c.out.hum&&(c.out.mist||c.out.exhaust)));
 assert(!(c.out.mist&&c.out.exhaust));
 if(c.out.heat)assert(c.heatReady(n)&&!c.heatLocked(n)&&!c.hot);
 if(c.out.hum||c.out.mist)assert(c.any(n)&&!c.waterLocked(n)&&c.high(n)<c.cfg.high_rh&&!c.hot);
}
static void scheduledMist(uint32_t origin=0){
 Controller c(origin);c.cfg.mist_period_min=5;c.cfg.mist_duration_sec=10;c.cfg.fae_period_min=0;ready(c,origin);
 std::vector<uint32_t> starts,ends;bool on=false;uint32_t finish=0;
 for(uint32_t dt=0;dt<=1050000;dt+=1000){uint32_t n=origin+dt;feed(c,n);c.tick(n);safe(c,n);
  if(c.out.mist&&!on)starts.push_back(dt);
  if(!c.out.mist&&on){ends.push_back(dt);finish=dt;assert(!c.mistPending);}
  if(c.state==State::MISTING||c.state==State::POST_MIST_SETTLE)assert(!c.mistPending);
  if(finish&&dt-finish<c.cfg.post_mist_sec*1000){assert(c.state==State::POST_MIST_SETTLE);assert(!c.out.heat&&!c.out.mist);}
  on=c.out.mist;
  if(dt==960000){assert(starts.size()==3&&ends.size()==3);}
 }
 assert((starts==std::vector<uint32_t>{300000,610000,920000}));
 for(size_t i=0;i<3;++i)assert(ends[i]-starts[i]==10000);
}
static void scheduledFae(){
 Controller c;c.cfg.mist_period_min=0;c.cfg.fae_period_min=5;c.cfg.fae_duration_sec=120;ready(c);
 std::vector<uint32_t> starts,ends;bool on=false;
 for(uint32_t n=0;n<=1380000;n+=1000){feed(c,n);c.tick(n);safe(c,n);if(c.out.exhaust&&!on)starts.push_back(n);if(!c.out.exhaust&&on){ends.push_back(n);assert(!c.faePending);}if(c.state==State::FAE)assert(!c.faePending);on=c.out.exhaust;
  if(n==960000)assert(starts.size()==2&&ends.size()==2);
 }
 assert((starts==std::vector<uint32_t>{300000,720000,1140000}));
 assert(ends.size()==3);for(size_t i=0;i<3;++i)assert(ends[i]-starts[i]==120000);
 // Same 16-minute/three-event shape with a 3-minute interval.
 c=Controller();c.cfg.mist_period_min=0;c.cfg.fae_period_min=3;c.cfg.fae_duration_sec=120;ready(c);starts.clear();ends.clear();on=false;
 for(uint32_t n=0;n<=960000;n+=1000){feed(c,n);c.tick(n);if(c.out.exhaust&&!on)starts.push_back(n);if(!c.out.exhaust&&on)ends.push_back(n);on=c.out.exhaust;}
 assert(starts.size()==3&&ends.size()==3);for(size_t i=0;i<3;++i)assert(ends[i]-starts[i]==120000);
}
static void glitchesAndDegraded(){
 for(State wanted:{State::HUMIDIFYING,State::MISTING,State::FAE}){
  Controller c;c.cfg.heater=Mode::OFF;c.cfg.mist_period_min=0;c.cfg.fae_period_min=0;ready(c,120000,27,27,60,60);
  if(wanted==State::MISTING)c.requestMist();if(wanted==State::FAE)c.requestExhaust();c.tick(120000);assert(c.state==wanted);
  // Both channels glitch twice: hold last good values, keep non-heater output.
  for(uint32_t n:{121000U,122000U}){c.top.ingest(false,0,0,n);c.bottom.ingest(false,0,0,n);c.tick(n);assert(c.state==wanted);assert(c.any(n));safe(c,n);}
  // Tick between the second and third read: no premature 2.5-s stale fault.
  for(uint32_t n=122050;n<123000;n+=50){c.tick(n);assert(c.state==wanted);safe(c,n);}
  // Third TOP failure, healthy BOTTOM: retain water/exhaust operation.
  c.top.ingest(false,0,0,123000);c.bottom.ingest(true,27,60,123000);c.tick(123000);assert(!c.both(123000)&&c.any(123000));assert(c.state==wanted);safe(c,123000);
  // Both unhealthy -> FAULT and all OFF.
  for(uint32_t n:{124000U,125000U,126000U}){c.top.ingest(false,0,0,n);c.bottom.ingest(false,0,0,n);c.tick(n);}
  assert(c.state==State::FAULT&&!c.out.heat&&!c.out.hum&&!c.out.mist&&!c.out.exhaust);
 }
 Controller c;ready(c,120000,24,23);c.tick(120000);assert(c.out.heat);
 c.top.ingest(false,0,0,121000);c.bottom.ingest(true,23,84,121000);c.tick(121000);
 assert(!c.out.heat&&c.state==State::POST_HEAT_COOLDOWN&&c.top.healthy&&c.top.usable(121000));
 // Start degraded with only TOP, then only BOTTOM: selected sensor drives water.
 for(bool useTop:{true,false}){Controller one;one.cfg.mist_period_min=0;one.cfg.fae_period_min=0;Sensor& s=useTop?one.top:one.bottom;for(int i=0;i<3;++i)s.ingest(true,22,60,120000);one.tick(120000);assert(one.out.hum&&!one.out.heat);assert(one.low(120000)==60&&one.high(120000)==60);}
}
static void starvation(){
 // Heater cannot reach target: preserve 10-min heat runs, then yield after wait.
 Controller c;c.cfg.mist_period_min=5;c.cfg.mist_duration_sec=10;c.cfg.fae_period_min=0;c.cfg.humidifier=Mode::OFF;ready(c,0,23,23,84,84);
 uint32_t start=0,end=0,lastHeatOff=0;bool oldHeat=false,oldMist=false;unsigned pulses=0;
 for(uint32_t n=0;n<=2700000;n+=1000){feed(c,n,23,23);c.tick(n);safe(c,n);if(oldHeat&&!c.out.heat)lastHeatOff=n;
  if(!oldHeat&&c.out.heat)assert(!c.mistStarved(n));
  if(!oldMist&&c.out.mist){++pulses;if(!start)start=n;assert(n-lastHeatOff>=120000);}
  if(oldMist&&!c.out.mist&&!end)end=n;
  oldHeat=c.out.heat;oldMist=c.out.mist;
 }
 assert(pulses>=1&&start>=300000+MIST_STARVATION_MS&&start<=300000+MIST_STARVATION_MS+MAX_HEAT_MS+120000);
 assert(end-start==10000);
 // Humidity demand that never reaches target gets one FULL bounded run; heat
 // then resumes, instead of preempting humidification on the following tick.
 c=Controller();c.cfg.mist_period_min=0;c.cfg.fae_period_min=0;ready(c,0,23,23,60,60);
 start=end=lastHeatOff=0;bool oldHum=false;oldHeat=false;bool resumed=false;
 for(uint32_t n=0;n<=4200000;n+=1000){feed(c,n,23,23,60,60);c.tick(n);safe(c,n);if(oldHeat&&!c.out.heat)lastHeatOff=n;
  if(!oldHeat&&c.out.heat){assert(!c.humStarved(n));if(end)resumed=true;}
  if(!oldHum&&c.out.hum&&!start){start=n;assert(n-lastHeatOff>=120000);}
  if(oldHum&&!c.out.hum&&!end)end=n;
  oldHum=c.out.hum;oldHeat=c.out.heat;
 }
 assert(start>=HUM_STARVATION_MS&&start<=HUM_STARVATION_MS+MAX_HEAT_MS+120000);
 assert(end-start==600000&&resumed);
 std::cout<<"PASS starvation: mist at 20-min overdue guard; humidifier full 600-s service, heat resumes\n";
}
static void calibrationReducerMigration(){
 ConfigV1 old;old.temperature_target=29.5;old.temperature_band=2.5;old.top_margin=1;old.humidity_target=88;old.humidity_band=6;old.post_heat_sec=180;old.post_mist_sec=240;old.hum_max_sec=300;old.mist_period_min=17;old.mist_duration_sec=11;old.fae_period_min=31;old.fae_duration_sec=73;old.high_rh=96;old.preheat_rh=91;old.purge_max_sec=111;old.purge_cooldown_sec=420;old.heater=Mode::OFF;old.humidifier=Mode::OFF;old.mist=Mode::OFF;old.exhaust=Mode::OFF;
 Config c;assert(decodeClimate(1,&old,sizeof(old),checksum(&old,sizeof(old)),c));
 assert(memcmp(&old,static_cast<ConfigV1*>(&c),sizeof(old))==0);
 assert(c.top_t_offset==0&&c.bottom_t_offset==0&&c.top_rh_offset==0&&c.bottom_rh_offset==0&&c.humidity_reducer==HumidityReducer::DRIEST);
 c.top_t_offset=3;c.bottom_t_offset=-3;c.top_rh_offset=10;c.bottom_rh_offset=-10;c.humidity_reducer=HumidityReducer::WETTEST;
 Config copy;assert(decodeClimate(SCHEMA,&c,sizeof(c),checksum(&c,sizeof(c)),copy));assert(copy.top_t_offset==3&&copy.bottom_rh_offset==-10&&copy.humidity_reducer==HumidityReducer::WETTEST);
 assert(!decodeClimate(1,&old,sizeof(old),0,copy));assert(!decodeClimate(9,&old,sizeof(old),checksum(&old,sizeof(old)),copy));assert(!decodeClimate(1,&old,sizeof(old)-1,checksum(&old,sizeof(old)-1),copy));
 c.top_t_offset=3.01;assert(!validConfig(c));c.top_t_offset=0;c.bottom_rh_offset=-10.01;assert(!validConfig(c));c.bottom_rh_offset=0;c.humidity_reducer=(HumidityReducer)2;assert(!validConfig(c));
 Sensor s;for(int i=0;i<3;++i)s.ingest(true,59,104,120000,-3,-10);assert(s.usable(120000)&&s.t==56&&s.rh==94); // applied BEFORE validation
 s.ingest(true,25,95,121000,0,10);assert(!s.fresh); // no clamping to hide >100
 Controller x;x.cfg.top_t_offset=1;x.cfg.bottom_t_offset=-1;x.cfg.top_rh_offset=5;x.cfg.bottom_rh_offset=-5;ready(x,120000,26,26,70,90);assert(x.top.t==27&&x.bottom.t==25&&x.average(120000)==26);assert(x.low(120000)==75&&x.high(120000)==85);
 x.cfg.mist_period_min=0;x.cfg.fae_period_min=0;x.cfg.heater=Mode::OFF;x.tick(120000);assert(x.out.hum);Config next=x.cfg;next.humidity_reducer=HumidityReducer::WETTEST;x.applyConfig(next,120050);x.tick(120050);assert(!x.out.hum);
 // Wettest demand must never change saturation protection's wettest reducer.
 x.cfg.humidity_reducer=HumidityReducer::DRIEST;feed(x,121000,26,26,60,100);x.tick(121000);assert(!x.out.hum&&!x.out.mist);
 Controller hot;hot.cfg.top_t_offset=3;ready(hot,120000,35,24);hot.tick(120000);assert(hot.hot&&!hot.out.heat);
}
int main(){Controller wrap;wrap.mistPending=wrap.mistWaitTracking=wrap.humDemand=wrap.humWaitTracking=true;wrap.mistPendingAt=wrap.humDemandAt=0xfff00000U;assert(!wrap.mistStarved(wrap.mistPendingAt+MIST_STARVATION_MS-1));assert(wrap.mistStarved(wrap.mistPendingAt+MIST_STARVATION_MS));assert(!wrap.humStarved(wrap.humDemandAt+HUM_STARVATION_MS-1));assert(wrap.humStarved(wrap.humDemandAt+HUM_STARVATION_MS));scheduledMist();scheduledMist(0xfff00000U);scheduledFae();glitchesAndDegraded();starvation();calibrationReducerMigration();char id[20],suffix[5];formatDeviceIdentity(0x8C235C470968ULL,id,suffix);assert(!strcmp(id,"6809475C238C")&&!strcmp(suffix,"238C"));char other[20],otherSuffix[5];formatDeviceIdentity(0x8D235C470968ULL,other,otherSuffix);assert(strcmp(suffix,otherSuffix));std::cout<<"PASS 1.0.2 regressions: schedules/settle, glitches/degraded, starvation, calibrated reducers, schema migration and MAC order\n";}
