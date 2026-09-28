#pragma once
#include "control.h"
// Read-only presentation of controller timers/interlocks. Never schedules outputs.
namespace chamber {
inline uint32_t remainingSec(uint32_t now,uint32_t since,uint32_t duration){
  uint32_t used=elapsed(now,since);return used>=duration?0:(duration-used+999)/1000;
}
struct StatusView {
  uint32_t heat_lock_remaining_sec=0,water_lock_remaining_sec=0,state_elapsed_sec=0;
  uint32_t mist_remaining_sec=0,hum_runtime_remaining_sec=0,purge_remaining_sec=0,purge_cooldown_remaining_sec=0;
  uint32_t heat_runtime_remaining_sec=0,fae_remaining_sec=0,hum_pause_remaining_sec=0,preheat_settle_remaining_sec=0;
  const char* blocked_by[4]={nullptr,nullptr,nullptr,nullptr}; // heater, humidifier, mist, exhaust
};
inline StatusView statusView(const Controller& c,uint32_t n){
  StatusView v;
  auto boot=remainingSec(n,c.bootAt,BOOT_GUARD_MS);
  auto postHeat=c.heatStopped?remainingSec(n,c.heatOffAt,c.waterGuardMs):0;
  auto postMist=c.mistStopped?remainingSec(n,c.mistOffAt,c.mistGuardMs):0;
  v.water_lock_remaining_sec=std::max(boot,postHeat);
  v.heat_lock_remaining_sec=std::max(v.water_lock_remaining_sec,postMist);
  v.state_elapsed_sec=elapsed(n,c.stateAt)/1000;
  if(c.state==State::MISTING)v.mist_remaining_sec=remainingSec(n,c.stateAt,c.cfg.mist_duration_sec*1000);
  if(c.state==State::HUMIDIFYING)v.hum_runtime_remaining_sec=remainingSec(n,c.stateAt,c.cfg.hum_max_sec*1000);
  if(c.state==State::HEATING)v.heat_runtime_remaining_sec=remainingSec(n,c.stateAt,MAX_HEAT_MS);
  if(c.state==State::FAE)v.fae_remaining_sec=remainingSec(n,c.stateAt,c.cfg.fae_duration_sec*1000);
  if(c.state==State::PREHEAT_PURGE||c.state==State::HIGH_RH_PURGE)v.purge_remaining_sec=remainingSec(n,c.stateAt,c.cfg.purge_max_sec*1000);
  if(c.purgeDone)v.purge_cooldown_remaining_sec=remainingSec(n,c.purgeOffAt,c.cfg.purge_cooldown_sec*1000);
  if(c.humPaused)v.hum_pause_remaining_sec=remainingSec(n,c.humPauseAt,HUM_PAUSE_MS);
  if(c.preheatWaiting)v.preheat_settle_remaining_sec=remainingSec(n,c.preheatOffAt,PURGE_SETTLE_MS);
  const Mode modes[]={c.cfg.heater,c.cfg.humidifier,c.cfg.mist,c.cfg.exhaust};
  const bool on[]={c.out.heat,c.out.hum,c.out.mist,c.out.exhaust};
  for(int i=0;i<4;++i){
    const char*& why=v.blocked_by[i];
    if(on[i])continue;
    if(c.hot){why="high temperature";continue;}
    if(c.ota||c.shutdown){why=c.ota?"firmware update":"restart pending";continue;}
    if(!c.any(n)){why="both sensors unusable";continue;}
    if(modes[i]==Mode::OFF){why="mode OFF";continue;}
    if(i==0&&!c.heatReady(n)){why=c.both(n)?"sensor read glitch":"sensor degraded";continue;}
    if(boot){why="startup lockout";continue;}
    if(postHeat){why="post-heat lockout";continue;}
    if(i==0&&postMist){why="post-mist settle";continue;}
    if(c.state==State::HEATING){why="heating";continue;}
    if(c.state==State::MISTING){why="misting";continue;}
    if(c.state==State::PREHEAT_PURGE||c.state==State::HIGH_RH_PURGE){why="purging";continue;}
    if(c.state==State::PREHEAT_SETTLE&&v.preheat_settle_remaining_sec){why="preheat settle";continue;}
    if(i==0&&(c.mistStarved(n)||c.humStarved(n)||c.humPriorityRun)){why="overdue water demand";continue;}
    if((i==1||i==2)&&c.high(n)>=c.cfg.high_rh){why="high humidity";continue;}
    if(i==0&&c.heatPending&&c.high(n)>=c.cfg.preheat_rh&&!c.preheatReady&&c.cfg.exhaust==Mode::AUTO&&v.purge_cooldown_remaining_sec){why="purge cooldown";continue;}
    if(c.humPriorityRun&&c.state==State::HUMIDIFYING){why="humidifying";continue;}
    if(i!=0&&c.heatPending&&!(i==1&&c.humStarved(n))&&!(i==2&&c.mistStarved(n))){why="heat pending";continue;}
    if((i==1||i==3)&&postMist){why="post-mist settle";continue;}
    if(c.state==State::FAE&&i!=3){why="ventilating";continue;}
    if(i==1&&v.hum_pause_remaining_sec){why="humidifier pause";continue;}
    if(i==1&&c.mistPending){why="mist pending";continue;}
    if(i==1&&(c.faePending||c.manualExhaust)){why="ventilation pending";continue;}
    if(i==3&&c.high(n)>=c.cfg.high_rh&&v.purge_cooldown_remaining_sec){why="purge cooldown";continue;}
  }
  return v;
}
}
