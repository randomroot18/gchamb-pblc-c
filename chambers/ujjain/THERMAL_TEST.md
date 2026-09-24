# Ujjain chamber thermal characterization sheet

Have the 230 V heater branch inspected and an **independent manual-reset overtemperature cutout** installed in series with heater power before the energized tests. An SSR can fail closed; a software cutoff is not the sole safety device. Start with the heater command disabled and verify the two sensors, relay OFF levels, earthing, covered terminals and cutoff operation without a live heater.

Log CSV columns: `UTC time, seconds, room °C, S1 °C, S2 °C, independent air probe °C, hottest accessible guard/surface °C, heater command, SSR load voltage, exhaust state, RH1, RH2, notes`. Place S1 and S2 at their intended fixed chamber positions; use an independent calibrated probe at the likely hottest tray position and a surface probe near the heater guard, away from mains conductors.

1. With the normal chamber load and door shut, record 10 minutes of heater-OFF baseline every 10 seconds. Note ambient and sensor difference.
2. Under supervision and using a separately approved **temporary test control** with an independent thermal cutout, run one **short 30-second heater pulse**, then turn it OFF and log every 10 seconds until all air probes have clearly peaked and cooled, at least 20 minutes. Stop immediately on unexpected hot spots, sensor dropout, relay chatter, cutout trip or increasing temperature beyond the agreed product limit.
3. If the first pulse is safe, repeat at a different initial chamber temperature. Longer pulses (for example 60 and 90 seconds) require a review of the first result and the guard temperature. Do not jump straight to the 4–5 minute full heat-up period.
4. For each run, report: temperature at OFF, largest later temperature, seconds from OFF to peak, largest S1/S2 disagreement, hottest independent air probe, and guard temperature. Repeat with exhaust on if that is a normal operating state.

Send the log and the intended crop's allowable chamber air-temperature range. We can then set a conservative target, `earlyOffC`, `hardStopC`, maximum ON pulse and minimum OFF dwell. The current 2 °C early margin, 35 °C software high limit, 90-second ON cap and 15-minute OFF dwell are placeholders, **not calibrated setpoints**. Run relay-only hardware tests again before enabling `HEATER_COMMISSIONED` in a reviewed build.
