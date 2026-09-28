# 1.0.4-ujjain

Base: 1.0.2 merged (Astra 1.0.2 + TLS memory fix + heater on GPIO19/CH3).

- Banner back to UJJAIN PILOT (production label).
- ntfy.sh telemetry restored, same private topic as 0.1.2:
  - routine status every 10 min (priority 1)
  - immediate ALERT on a new fault: high temperature or a sensor offline (priority 4,
    at most one new alert per 5 min)
  - one RECOVERED message when the fault clears (priority 3)
  - runs in the HTTPS task, skipped during OTA, never affects outputs
  - last result in /api/status "ntfy"; failures logged in /api/events
- ISRG Root X2 added to the TLS bundle (Let's Encrypt ECDSA chains).
- LOW MEMORY advisory when idle free heap < 80,000 or largest block < 32,000.

Climate logic, pins, partitions, NVS layout unchanged from 1.0.2/1.0.3.

## Access policy (added)
- Anyone on the chamber's network can view and change climate settings: targets,
  heater AUTO/OFF, humidifier, mist, exhaust, Advanced, chamber name, update check.
  No code prompt for these. Safety interlocks still apply (no forced heater ON).
- The local control code is required only for: Wi-Fi changes, OTA URL / remote service
  settings, installing an update, restart, restore climate defaults, forget Wi-Fi,
  factory reset. The browser remembers it after the first entry.
- The code is always visible: bottom line of the TFT ("CODE xxxxxxxx  shunya-xxxx.local")
  whenever no alert is showing, on the setup screen, and in the Serial boot log.
- Wi-Fi scan no longer needs the code.
Tests: control, regression, status view, UI harness and the TFT renderer test all pass.
