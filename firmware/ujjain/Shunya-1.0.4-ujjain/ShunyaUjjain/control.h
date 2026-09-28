#pragma once
#include "config.h"
#include <cmath>
#include <cstring>
#include <algorithm>
namespace chamber {
inline uint32_t elapsed(uint32_t now,uint32_t then){return now-then;}
enum class Mode:uint8_t {AUTO,OFF};
enum class State:uint8_t {BOOT_SAFE,IDLE,HEATING,POST_HEAT_COOLDOWN,PREHEAT_PURGE,PREHEAT_SETTLE,HUMIDIFYING,MISTING,POST_MIST_SETTLE,HIGH_RH_PURGE,FAE,FAULT,OTA_SAFE};
inline const char* stateName(State s){static const char* names[]={"BOOT_SAFE","IDLE","HEATING","POST_HEAT_COOLDOWN","PREHEAT_PURGE","PREHEAT_SETTLE","HUMIDIFYING","MISTING","POST_MIST_SETTLE","HIGH_RH_PURGE","FAE","FAULT","OTA_SAFE"};return names[(unsigned)s];}
enum class HumidityReducer:uint8_t {DRIEST,WETTEST};
struct ConfigV1 {
  float temperature_target=27,temperature_band=3,top_margin=2;
  float humidity_target=85,humidity_band=5;
  uint32_t post_heat_sec=120,post_mist_sec=120,hum_max_sec=600;
  uint32_t mist_period_min=180,mist_duration_sec=10,fae_period_min=60,fae_duration_sec=120;
  float high_rh=95,preheat_rh=90;
  uint32_t purge_max_sec=120,purge_cooldown_sec=300;
  Mode heater=Mode::AUTO,humidifier=Mode::AUTO,mist=Mode::AUTO,exhaust=Mode::AUTO;
};
// Exact schema-1 prefix retained for NVS migration.
struct Config: ConfigV1 {
  float top_t_offset=0,top_rh_offset=0,bottom_t_offset=0,bottom_rh_offset=0;
  HumidityReducer humidity_reducer=HumidityReducer::DRIEST;
};
static_assert(sizeof(ConfigV1)==68,"Unexpected schema-1 layout");
inline bool validConfig(const Config& c){
  auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
  return range(c.top_t_offset,-3,3)&&range(c.bottom_t_offset,-3,3)&&range(c.top_rh_offset,-10,10)&&range(c.bottom_rh_offset,-10,10)
    &&(unsigned)c.humidity_reducer<=1&&range(c.temperature_target,20,35)&&range(c.temperature_band,.5,8)&&range(c.top_margin,.5,2)
    &&range(c.humidity_target,30,95)&&range(c.humidity_band,1,20)
    &&c.post_heat_sec>=120&&c.post_heat_sec<=900&&c.post_mist_sec>=60&&c.post_mist_sec<=900
    &&c.hum_max_sec>=30&&c.hum_max_sec<=600&&c.mist_period_min<=720&&c.mist_duration_sec>=1&&c.mist_duration_sec<=120
    &&c.fae_period_min<=720&&c.fae_duration_sec>=10&&c.fae_duration_sec<=300
    &&range(c.high_rh,93,98)&&range(c.preheat_rh,80,95)&&c.preheat_rh<c.high_rh
    &&c.purge_max_sec>=10&&c.purge_max_sec<=120&&c.purge_cooldown_sec>=60&&c.purge_cooldown_sec<=1800
    &&(unsigned)c.heater<=1&&(unsigned)c.humidifier<=1&&(unsigned)c.mist<=1&&(unsigned)c.exhaust<=1;
}
struct Sensor {
  float t=NAN,rh=NAN; bool healthy=false,fresh=false,ever=false;
  uint8_t good=0,bad=0; uint32_t lastGood=0,lastAttempt=0;
  void ingest(bool ok,float temp,float humidity,uint32_t now,float tOffset=0,float rhOffset=0){
    lastAttempt=now;
    temp+=tOffset;humidity+=rhOffset; // Calibrate BEFORE plausibility checks and reducers; never clamp invalid RH.
    fresh=ok&&std::isfinite(temp)&&std::isfinite(humidity)&&temp>=MIN_SENSOR_C&&temp<=MAX_SENSOR_C&&humidity>=0&&humidity<=100;
    if(fresh){t=temp;rh=humidity;lastGood=now;bad=0;if(good<3)++good;if(good>=3){healthy=true;ever=true;}}
    else{good=0;if(bad<3)++bad;if(bad>=3){healthy=false;t=rh=NAN;}}
  }
  // Non-heater control may bridge one/two glitches with the last good sample.
  // Three failed attempts remove it; a stalled acquisition stream still expires.
  bool usable(uint32_t now) const{return healthy&&elapsed(now,lastAttempt)<=SENSOR_STALE_MS;}
};
struct Outputs {bool heat=false,hum=false,mist=false,exhaust=false;};
struct Controller {
  Config cfg; Sensor top,bottom; State state=State::BOOT_SAFE; Outputs out;
  bool heatPending=false,mistPending=false,faePending=false,manualMist=false,manualHum=false,manualExhaust=false;
  bool hot=false,ota=false,shutdown=false,purgeIneffective=false,disagreement=false;
  bool heatStopped=false,mistStopped=false,humPaused=false,purgeDone=false,preheatReady=false,preheatWaiting=false;
  bool recoveryTiming=false,clearTiming=false,spreadTiming=false;
  bool mistWaitTracking=false,humWaitTracking=false,humDemand=false,humPriorityRun=false;
  uint32_t mistPendingAt=0,humDemandAt=0;
  uint8_t ineffectiveCount=0;
  uint32_t bootAt=0,stateAt=0,heatOffAt=0,mistOffAt=0,humPauseAt=0,purgeOffAt=0,preheatOffAt=0;
  uint32_t mistDueAt=0,faeDueAt=0,manualHumAt=0,recoveryAt=0,clearAt=0,spreadAt=0;
  uint32_t waterGuardMs=BOOT_GUARD_MS,mistGuardMs=120000;
  float purgeStart=NAN;
  const char* reason="Waiting for sensors and boot cooldown";
  explicit Controller(uint32_t now=0):bootAt(now),stateAt(now),mistDueAt(now),faeDueAt(now){}
  bool both(uint32_t n)const{return top.usable(n)&&bottom.usable(n);}
  bool heatReady(uint32_t n)const{return both(n)&&top.fresh&&bottom.fresh;}
  bool any(uint32_t n)const{return top.usable(n)||bottom.usable(n);}
  float low(uint32_t n)const {if(both(n))return std::min(top.rh,bottom.rh);return top.usable(n)?top.rh:bottom.usable(n)?bottom.rh:NAN;}
  float high(uint32_t n)const {if(both(n))return std::max(top.rh,bottom.rh);return top.usable(n)?top.rh:bottom.usable(n)?bottom.rh:NAN;}
  float humidityDemandRH(uint32_t n)const{return cfg.humidity_reducer==HumidityReducer::WETTEST?high(n):low(n);}
  bool mistStarved(uint32_t n)const{return mistPending&&mistWaitTracking&&elapsed(n,mistPendingAt)>=MIST_STARVATION_MS;}
  bool humStarved(uint32_t n)const{return humDemand&&humWaitTracking&&elapsed(n,humDemandAt)>=HUM_STARVATION_MS;}
  float average(uint32_t n)const{return both(n)?(top.t+bottom.t)/2:NAN;}
  bool bootGuard(uint32_t n)const{return elapsed(n,bootAt)<BOOT_GUARD_MS;}
  bool waterLocked(uint32_t n)const{return bootGuard(n)||(heatStopped&&elapsed(n,heatOffAt)<waterGuardMs);}
  bool heatLocked(uint32_t n)const{return bootGuard(n)||(heatStopped&&elapsed(n,heatOffAt)<waterGuardMs)||(mistStopped&&elapsed(n,mistOffAt)<mistGuardMs);}
  bool safeToReboot(uint32_t n)const{return !waterLocked(n)&&!out.heat&&!out.mist&&!out.hum&&!out.exhaust;}
  void change(State s,uint32_t n,const char* why){
    if(s==state)return;
    if(state==State::HEATING){heatStopped=true;heatOffAt=n;waterGuardMs=cfg.post_heat_sec*1000;preheatReady=false;}
    if(state==State::MISTING){mistStopped=true;mistOffAt=n;mistGuardMs=cfg.post_mist_sec*1000;mistDueAt=n;manualMist=false;mistPending=false;mistWaitTracking=false;}
    if(state==State::FAE){faePending=false;manualExhaust=false;faeDueAt=n;}
    if(state==State::HUMIDIFYING){humPaused=true;humPauseAt=n;humPriorityRun=false;}
    state=s;stateAt=n;reason=why;
  }
  void applyConfig(const Config& c,uint32_t n){
    if(c.mist_period_min!=cfg.mist_period_min){mistDueAt=n;if(!manualMist){mistPending=false;mistWaitTracking=false;}}
    if(c.fae_period_min!=cfg.fae_period_min){faeDueAt=n;faePending=false;}
    if(c.mist==Mode::OFF){mistPending=false;manualMist=false;mistWaitTracking=false;}
    if(c.humidifier==Mode::OFF){manualHum=false;humDemand=false;humWaitTracking=false;humPriorityRun=false;}
    if(c.exhaust==Mode::OFF){manualExhaust=false;faePending=false;}
    if(c.heater==Mode::OFF){heatPending=false;preheatReady=false;}
    cfg=c;
  }
  void requestMist(){if(!mistPending&&state!=State::MISTING){mistPending=true;manualMist=true;}}
  void requestHum(uint32_t n){if(!manualHum){manualHum=true;manualHumAt=n;}}
  void requestExhaust(){manualExhaust=true;}
  void finishPurge(uint32_t n){
    float drop=purgeStart-high(n);
    if(!std::isfinite(drop)||drop<PURGE_DROP){if(ineffectiveCount<255)++ineffectiveCount;}else ineffectiveCount=0;
    if(ineffectiveCount>=2)purgeIneffective=true;
    purgeDone=true;purgeOffAt=n;
    if(state==State::PREHEAT_PURGE){preheatReady=true;preheatWaiting=true;preheatOffAt=n;change(State::PREHEAT_SETTLE,n,"Preheat purge complete; settling");}
    else change(State::IDLE,n,"Humidity purge complete; cooldown");
  }
  void tick(uint32_t n){
    // A fresh plausible over-temperature sample trips even during 3-read qualification.
    if((top.fresh&&elapsed(n,top.lastGood)<=SENSOR_STALE_MS&&top.t>=HARD_TEMP_C)||(bottom.fresh&&elapsed(n,bottom.lastGood)<=SENSOR_STALE_MS&&bottom.t>=HARD_TEMP_C))hot=true;
    if(hot){
      if(heatReady(n)&&top.t<=RECOVERY_TEMP_C&&bottom.t<=RECOVERY_TEMP_C){if(!recoveryTiming){recoveryTiming=true;recoveryAt=n;}if(elapsed(n,recoveryAt)>=HOT_RECOVERY_MS){hot=false;recoveryTiming=false;}}
      else recoveryTiming=false;
    }
    float rh=high(n);
    if(both(n)&&(std::fabs(top.t-bottom.t)>=4||std::fabs(top.rh-bottom.rh)>=15)){
      if(!spreadTiming){spreadTiming=true;spreadAt=n;}if(elapsed(n,spreadAt)>=DISAGREE_MS)disagreement=true;
    }else{spreadTiming=false;disagreement=false;}
    if(any(n)&&rh<PURGE_END_RH){if(!clearTiming){clearTiming=true;clearAt=n;}if(elapsed(n,clearAt)>=ADVISORY_CLEAR_MS){purgeIneffective=false;ineffectiveCount=0;}}else clearTiming=false;
    if(state!=State::MISTING&&state!=State::POST_MIST_SETTLE&&cfg.mist==Mode::AUTO&&cfg.mist_period_min&&elapsed(n,mistDueAt)>=cfg.mist_period_min*60000)mistPending=true;
    if(state!=State::FAE&&cfg.exhaust==Mode::AUTO&&cfg.fae_period_min&&elapsed(n,faeDueAt)>=cfg.fae_period_min*60000)faePending=true;
    if(mistPending){if(!mistWaitTracking){mistWaitTracking=true;mistPendingAt=n;}}else mistWaitTracking=false;
    if(cfg.humidifier==Mode::OFF){humDemand=false;humWaitTracking=false;}
    else if(any(n)){
      float demandRH=humidityDemandRH(n);
      if(demandRH>=cfg.humidity_target){humDemand=false;humWaitTracking=false;}
      else if(demandRH<=cfg.humidity_target-cfg.humidity_band)humDemand=true;
    }
    // Keep the waiting age through temporary unsafe conditions. A full bounded
    // humidifier run (or satisfied demand) releases its claim so heat also progresses.
    if(humDemand&&!humWaitTracking){humWaitTracking=true;humDemandAt=n;}
    bool waterPriority=mistStarved(n)||humStarved(n);
    if(manualHum&&elapsed(n,manualHumAt)>=MANUAL_HUM_MS){manualHum=false;humPaused=true;humPauseAt=n;}
    if(humPaused&&elapsed(n,humPauseAt)>=HUM_PAUSE_MS)humPaused=false;
    if(preheatWaiting&&elapsed(n,preheatOffAt)>=PURGE_SETTLE_MS)preheatWaiting=false;
    if(!heatReady(n)||cfg.heater==Mode::OFF)heatPending=false;
    else if(average(n)>=cfg.temperature_target||top.t>=cfg.temperature_target+cfg.top_margin)heatPending=false;
    else if(bottom.t<=cfg.temperature_target-cfg.temperature_band)heatPending=true;
    if(!heatPending&&state!=State::PREHEAT_PURGE&&state!=State::PREHEAT_SETTLE)preheatReady=false;
    // Priority: latched thermal fault -> explicit OTA/shutdown -> invalid sensors
    // -> boot guard -> finish active bounded state -> heat sequence -> water cooldown
    // -> saturation purge -> mist -> mist settle -> FAE -> humidity. A completed
    // preheat purge grants ONE heat cycle even if outdoor RH made purge ineffective.
    if(hot){change(State::FAULT,n,"HIGH TEMPERATURE - heater locked out");commit(n);return;}
    if(ota||shutdown){change(State::OTA_SAFE,n,ota?"Update approved; outputs OFF":"Restart requested; outputs OFF");commit(n);return;}
    // One usable sensor supports non-heater control; no usable sensors is a fault.
    if(!any(n)){change(State::FAULT,n,"Both sensors unusable - outputs OFF; retrying");commit(n);return;}
    if(bootGuard(n)){change(State::BOOT_SAFE,n,"Boot cooldown; validating sensors");commit(n);return;}
    switch(state){
      case State::HEATING:
        if(!heatReady(n)||cfg.heater==Mode::OFF||average(n)>=cfg.temperature_target||top.t>=cfg.temperature_target+cfg.top_margin||elapsed(n,stateAt)>=MAX_HEAT_MS){
          heatPending=false;change(State::POST_HEAT_COOLDOWN,n,"Heater OFF - sensor, target, top ceiling, mode or run limit");
        }commit(n);return;
      case State::MISTING:
        if(cfg.mist==Mode::OFF||rh>=cfg.high_rh||elapsed(n,stateAt)>=cfg.mist_duration_sec*1000)change(State::POST_MIST_SETTLE,n,"Mist complete / stopped safely");
        commit(n);return;
      case State::PREHEAT_PURGE:case State::HIGH_RH_PURGE:
        if(cfg.exhaust==Mode::OFF||rh<=PURGE_END_RH||purgeStart-rh>=PURGE_DROP||elapsed(n,stateAt)>=cfg.purge_max_sec*1000)finishPurge(n);
        commit(n);return;
      case State::PREHEAT_SETTLE:
        if(preheatWaiting){commit(n);return;}break;
      case State::FAE:
        if(cfg.exhaust==Mode::OFF||heatPending||elapsed(n,stateAt)>=cfg.fae_duration_sec*1000){manualExhaust=false;faeDueAt=n;change(State::IDLE,n,"Ventilation complete / heat pending");}
        else {commit(n);return;}break;
      case State::HUMIDIFYING:
        if(cfg.humidifier==Mode::OFF||(!manualHum&&humidityDemandRH(n)>=cfg.humidity_target)||elapsed(n,stateAt)>=cfg.hum_max_sec*1000||((heatPending||mistPending||faePending||manualExhaust)&&!humPriorityRun)||rh>=cfg.high_rh){
          if(humidityDemandRH(n)>=cfg.humidity_target||elapsed(n,stateAt)>=cfg.hum_max_sec*1000)humWaitTracking=false;
          if(elapsed(n,stateAt)>=cfg.hum_max_sec*1000)manualHum=false;
          change(State::IDLE,n,"Humidifier OFF; pause / interlock");
        }else{commit(n);return;}break;
      default:break;
    }
    if(heatPending&&!waterPriority&&!heatLocked(n)){
      if(preheatWaiting){change(State::PREHEAT_SETTLE,n,"Settling after purge");commit(n);return;}
      if(rh>=cfg.preheat_rh&&!preheatReady&&cfg.exhaust==Mode::AUTO){
        // Respect the purge rest interval even when a heat request arrives.
        if(purgeDone&&elapsed(n,purgeOffAt)<cfg.purge_cooldown_sec*1000){change(State::IDLE,n,"Heat pending - purge cooldown");commit(n);return;}
        purgeStart=rh;change(State::PREHEAT_PURGE,n,"Preheat purge started");
      }else change(State::HEATING,n,"Heater ON - bottom requests heat");
      commit(n);return;
    }
    if(waterLocked(n)){change(State::POST_HEAT_COOLDOWN,n,"Residual heater heat - water locked out");commit(n);return;}
    if(rh>=cfg.high_rh&&cfg.exhaust==Mode::AUTO&&(!purgeDone||elapsed(n,purgeOffAt)>=cfg.purge_cooldown_sec*1000)){
      purgeStart=rh;change(State::HIGH_RH_PURGE,n,"High humidity purge started");commit(n);return;
    }
    if(mistPending&&cfg.mist==Mode::AUTO&&rh<cfg.high_rh&&(!heatPending||mistStarved(n))){mistPending=false;change(State::MISTING,n,"Mist started");commit(n);return;}
    if(mistStopped&&elapsed(n,mistOffAt)<mistGuardMs){change(State::POST_MIST_SETTLE,n,"Droplet settling - heater locked out");commit(n);return;}
    if((faePending||manualExhaust)&&cfg.exhaust==Mode::AUTO&&!heatPending){faePending=false;change(State::FAE,n,"Ventilation started");commit(n);return;}
    if(cfg.humidifier==Mode::AUTO&&!humPaused&&rh<cfg.high_rh&&(!heatPending||humStarved(n))&&(manualHum||humidityDemandRH(n)<=cfg.humidity_target-cfg.humidity_band||humStarved(n))){bool priority=humStarved(n);change(State::HUMIDIFYING,n,"Humidifier ON");humPriorityRun=priority;}
    else change(State::IDLE,n,"Waiting for demand / pending interlock");
    commit(n);
  }
  void commit(uint32_t n){
    out={};
    if(hot||ota||shutdown||!any(n)||bootGuard(n))return;
    switch(state){
      case State::HEATING:out.heat=cfg.heater==Mode::AUTO&&heatReady(n)&&!heatLocked(n);break;
      case State::HUMIDIFYING:out.hum=cfg.humidifier==Mode::AUTO&&!waterLocked(n)&&high(n)<cfg.high_rh;break;
      case State::MISTING:out.mist=cfg.mist==Mode::AUTO&&!waterLocked(n)&&high(n)<cfg.high_rh;break;
      case State::FAE:case State::PREHEAT_PURGE:case State::HIGH_RH_PURGE:out.exhaust=cfg.exhaust==Mode::AUTO;break;
      default:break;
    }
  }
};
}
