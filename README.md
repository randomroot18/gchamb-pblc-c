# Shunya Jen chamber — bench firmware

This repository is the independent codebase for the Jen/Ujjain ESP32-WROOM-32U chamber. The source was adapted from the Shunya prototype sketch. **This is a bench build, not heater-ready field firmware.** The heating output is compile-time disabled (`HEATER_COMMISSIONED = false`); remote OTA is also disabled until authentication and rollback are completed. Never treat a passing software test as approval to energize a 230 V heater.

## Physical pin map

| GPIO | Connected function | Firmware behavior |
|---|---|---|
| 26 | Four-channel CH1 → 5 V humidifier | Active LOW |
| 25 | Four-channel CH2 → 24 V misting load | Active LOW, timed pulse |
| 19 | Four-channel CH3, no recirculation fan | Always driven HIGH (OFF), leave contact side empty |
| 27 | Four-channel CH4 → 12 V exhaust fan | Active LOW |
| 13 | Separate single relay → 5 V SSR control → AC heater | Active LOW, **locked OFF** in this build |
| 21/22 | SHT4x sensor 1 SDA/SCL | Separate I²C bus |
| 32/33 | SHT4x sensor 2 SDA/SCL | Separate I²C bus |
| 14/17/16/18/23/34 | TFT CS/DC/RST/SCK/MOSI/MISO | ILI9341 at **10 MHz**, touch absent |

This follows the attached Rev A wiring guide except the Jen chamber **has no recirculation fan**. Confirm the real CH1/CH2/CH4 contact wiring against labels before enabling loads. GPIO12 remains unused. The 5 V active-LOW relay inputs require a hardware OFF bias while the ESP32 is booting or reset; software cannot prevent a startup glitch before `setup()`.

## What changed

- A four-device UI lists humidifier, misting, heater and exhaust. No recirculation scheduler remains. Sensor RH disagreement reports **SENSOR CHECK** and pauses automatic humidification.
- The dedicated heater policy uses the higher of two valid temperatures, stops on disagreement, sensor loss, software high limit (35 °C), an early cutoff, or a 90-second run cap. It waits 15 minutes after an OFF event or boot before allowing a new run. The 2 °C early-off margin is only a placeholder. Heater commissioning is disabled until measured coast and an independent manual-reset high-limit cutoff are verified.
- The web API rejects manual heater ON. If both sensors are invalid, humidifier and misting also turn OFF. The display keeps the known working 10 MHz SPI clock.
- The firmware sends an optional read-only telemetry snapshot every 30 seconds **outbound over TLS**. `src/telemetry_private.h` is untracked; copy `src/telemetry_private.example.h`, provide the HTTPS ingest URL and a unique 32+ character device token, and flash locally. The collector stores observations in SQLite and serves a password-protected dashboard. The control loop never waits for the network.
- The inherited local web API has no authentication, and the inherited provisioning AP password is predictable from the MAC. Keep it on a trusted LAN during bench work. Do not forward port 80 to the internet. OTA code is present but deliberately disabled; it lacks authenticated control and automatic first-boot rollback.

## Build and bench checks

1. With PlatformIO, run `pio run -e jen_esp32u` from this directory. It selects `esp32dev` and the OTA-capable `min_spiffs.csv` partition table. Confirm your exact board has 4 MB flash. **Firmware compilation has not been run in this environment; GitHub CI is configured to attempt it.**
2. Run `g++ -std=c++17 -Wall -Wextra -Werror -I include tests/heater_policy_test.cpp -o /tmp/jen-heater-test && /tmp/jen-heater-test` and `python3 tests/collector_test.py`.
3. Disconnect heater mains and all relay contact-side loads. Verify each relay IN is HIGH during boot and after reset, especially GPIO13 and GPIO19. Check serial at **115200** and the TFT at 10 MHz. Check two I²C sensors independently. Confirm `/api/status` shows exactly four devices and `heater.commissioned:false`.
4. On a trusted 2.4 GHz Wi-Fi network, provision via local AP and check `/api/status`. For collector bench work: set `JEN_DEVICE_TOKEN` (32+ random chars), `JEN_ADMIN_PASSWORD` (16+ random chars), run `python3 collector/server.py`, and point a **local test** telemetry URL through a TLS reverse proxy. The firmware refuses plain HTTP. The dashboard uses HTTP Basic auth (`admin` and your password) and must be accessed over HTTPS when remote.

## Remote architecture and deployment gaps

The Jen device may be on another network; a computer at your office cannot poll its private IP. The firmware therefore **pushes** telemetry to an internet-reachable HTTPS endpoint. A normally-on computer can run this collector behind a TLS reverse proxy or a correctly configured Tailscale Funnel ingress. You can inspect its dashboard through the same HTTPS endpoint with admin auth. The collector binds `127.0.0.1:8765` by default; set a unique device token, admin password, database backup and HTTPS ingress before remote use. There is no live endpoint, domain, token or machine connected yet; cross-network streaming remains unverified.

For production OTA, add a second OTA application partition, signed releases tied to this hardware family, authenticated administration, first-boot health/rollback, staged rollout and a proven recovery path. The source prototype's manifest + hash checks establish download integrity, but SHA-256 alone does not authenticate who may replace a manifest. Do not turn its legacy OTA scheduler back on as-is.

## Thermal test for the real heater

Read `THERMAL_TEST.md` before any heater power. Collect time series and choose early shutoff from observed maximum **post-OFF** rise, sensor placement error and a safety margin. The chamber has no recirculation fan, so sensor disagreement and local hot spots matter. Independent hardware overtemperature protection, earth continuity, enclosure inspection and safe heater wiring are prerequisites to any energized test.
