#include "../ShunyaUjjain/status_view.h"
#include <cassert>
#include <iostream>
using namespace chamber;
static void ready(Controller& c,uint32_t n){for(int i=0;i<3;++i){c.top.ingest(true,27,84,n);c.bottom.ingest(true,27,84,n);}}
int main(){
 Controller c;ready(c,0);auto v=statusView(c,0);assert(v.heat_lock_remaining_sec==120&&v.water_lock_remaining_sec==120);assert(!strcmp(v.blocked_by[0],"startup lockout"));
 ready(c,120000);c.tick(120000);v=statusView(c,120000);assert(v.heat_lock_remaining_sec==0);for(auto b:v.blocked_by)assert(!b);
 c.state=State::HEATING;c.stateAt=120000;c.out.heat=true;v=statusView(c,121001);assert(v.state_elapsed_sec==1&&v.heat_runtime_remaining_sec==599);assert(!v.blocked_by[0]);assert(!strcmp(v.blocked_by[1],"heating"));
 c.out={};c.heatStopped=true;c.heatOffAt=120000;c.waterGuardMs=120000;v=statusView(c,121001);assert(v.heat_lock_remaining_sec==119&&v.water_lock_remaining_sec==119);assert(!strcmp(v.blocked_by[2],"post-heat lockout"));
 c=Controller();ready(c,120000);c.state=State::MISTING;c.stateAt=120000;c.out.mist=true;v=statusView(c,123001);assert(v.mist_remaining_sec==7);assert(!v.blocked_by[2]);
 c.out={};c.mistStopped=true;c.mistOffAt=120000;c.mistGuardMs=120000;c.state=State::POST_MIST_SETTLE;v=statusView(c,121000);assert(v.heat_lock_remaining_sec==119&&v.water_lock_remaining_sec==0);assert(!strcmp(v.blocked_by[0],"post-mist settle"));assert(!strcmp(v.blocked_by[3],"post-mist settle"));
 c=Controller();ready(c,120000);c.state=State::HUMIDIFYING;c.stateAt=120000;c.out.hum=true;v=statusView(c,121000);assert(v.hum_runtime_remaining_sec==599&&!v.blocked_by[1]);
 c.state=State::HIGH_RH_PURGE;c.out={};c.out.exhaust=true;v=statusView(c,121000);assert(v.purge_remaining_sec==119&&!v.blocked_by[3]);
 c.out={};c.state=State::IDLE;c.purgeDone=true;c.purgeOffAt=120000;c.heatPending=true;c.top.rh=c.bottom.rh=91;v=statusView(c,121000);assert(v.purge_cooldown_remaining_sec==299);assert(!strcmp(v.blocked_by[0],"purge cooldown"));
 c=Controller();ready(c,120000);c.top.ingest(false,0,0,121000);v=statusView(c,121000);assert(!strcmp(v.blocked_by[0],"sensor read glitch")&&!v.blocked_by[1]);
 for(int i=0;i<3;++i)c.top.ingest(false,0,0,121000);v=statusView(c,121000);assert(!strcmp(v.blocked_by[0],"sensor degraded")&&!v.blocked_by[2]);
 for(int i=0;i<3;++i)c.bottom.ingest(false,0,0,121000);v=statusView(c,121000);for(auto b:v.blocked_by)assert(!strcmp(b,"both sensors unusable"));
 c=Controller();ready(c,120000);c.cfg.heater=Mode::OFF;v=statusView(c,120000);assert(!strcmp(v.blocked_by[0],"mode OFF"));
 c.hot=true;v=statusView(c,120000);for(auto b:v.blocked_by)assert(!strcmp(b,"high temperature"));
 c.hot=false;c.ota=true;v=statusView(c,120000);for(auto b:v.blocked_by)assert(!strcmp(b,"firmware update"));
 assert(remainingSec(0x00000010,0xfffffff0,1000)==1);assert(remainingSec(1000,0,1000)==0);assert(remainingSec(999,0,1000)==1);
 // Presentation must not change any controller bytes or scheduling behavior.
 Controller frozen=c;statusView(c,123456);assert(!memcmp(&c,&frozen,sizeof(c)));
 std::cout<<"PASS: status countdowns, ceil/expiry/rollover, per-output inhibit reasons, read-only presentation\n";
}
