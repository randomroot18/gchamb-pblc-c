#pragma once
#include <cmath>
#include <cstdint>

// Pure control logic; independent of Arduino so the safety decisions can be tested.
namespace heater {
struct Config {
  float targetC = 22.0f;
  float earlyOffC = 2.0f;      // Placeholder: replace with measured coast rise + margin.
  float hardStopC = 35.0f;     // Software limit; independent physical high-limit required.
  float sensorSpreadC = 1.5f;
  uint32_t maxOnMs = 90UL * 1000UL;
  uint32_t minOffMs = 15UL * 60UL * 1000UL;
};
struct State {
  bool on = false;
  bool tripped = false;
  uint32_t onAt = 0;
  uint32_t offAt = 0;
  uint32_t bootAt = 0;
  bool started = false;
};
// The caller must assert physical output OFF before calling step on boot.
inline bool step(State& s, const Config& c, uint32_t now,
                 bool firstOk, float firstC, bool secondOk, float secondC,
                 bool commissioned) {
  if (!s.started) { s.started = true; s.bootAt = now; s.offAt = now; }
  const bool valid = firstOk && secondOk && std::isfinite(firstC) &&
                     std::isfinite(secondC);
  if (!valid) { s.on = false; s.offAt = now; return false; }
  const float high = firstC > secondC ? firstC : secondC;
  const float spread = std::fabs(firstC - secondC);
  if (high >= c.hardStopC) s.tripped = true;
  if (s.tripped || !commissioned || spread > c.sensorSpreadC) {
    s.on = false; s.offAt = now; return false;
  }
  // Both sensors have to be below the early shutoff threshold. After any OFF,
  // including a reset, the heater stays out for the full thermal coast period.
  const float stopC = c.targetC - c.earlyOffC;
  if (s.on) {
    if (high >= stopC || (uint32_t)(now - s.onAt) >= c.maxOnMs) {
      s.on = false; s.offAt = now;
    }
  } else if ((uint32_t)(now - s.offAt) >= c.minOffMs && high < stopC - 0.5f) {
    s.on = true; s.onAt = now;
  }
  return s.on;
}
} // namespace heater
