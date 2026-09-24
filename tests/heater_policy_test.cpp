#include "HeaterPolicy.h"
#include <cassert>
int main() {
  heater::Config c;
  heater::State s;
  assert(!heater::step(s,c,0,true,18,true,18,false));
  assert(!heater::step(s,c,900000,true,18,true,18,false));
  assert(!heater::step(s,c,900001,true,18,true,18,true));
  assert( heater::step(s,c,1800001,true,18,true,18,true));
  assert(!heater::step(s,c,1801000,true,20,true,19,true)); // early off
  assert(!heater::step(s,c,1801001,true,18,true,18,true)); // cooldown
  assert( heater::step(s,c,2701000,true,18,true,18,true));
  assert(!heater::step(s,c,2702000,true,18,false,18,true)); // missing
  assert(!heater::step(s,c,3602000,true,18,true,21,true)); // disagreement
  assert(!heater::step(s,c,4502000,true,35,true,18,true)); // hard trip
  assert(!heater::step(s,c,5402000,true,18,true,18,true)); // latched
  heater::State run;
  heater::step(run,c,0,true,18,true,18,true);
  assert( heater::step(run,c,900000,true,18,true,18,true));
  assert(!heater::step(run,c,990000,true,18,true,18,true)); // 90 s cap
  assert(!heater::step(run,c,990001,true,18,true,18,true));
  heater::State reboot;
  assert(!heater::step(reboot,c,0,true,18,true,18,true));
  assert(!heater::step(reboot,c,899999,true,18,true,18,true));
  assert( heater::step(reboot,c,900000,true,18,true,18,true));
}
