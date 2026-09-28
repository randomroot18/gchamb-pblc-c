#pragma once
#include <stdint.h>
// Ujjain maintenance release; hardware and safety constants retained.
#define FW_VERSION "1.0.4-ujjain"
#define FW_BANNER "UJJAIN PILOT"
#define FW_DEVICE_FAMILY "shunya-ujjain-chamber"
// ntfy.sh push telemetry (outbound HTTPS only). The topic is public to anyone who knows
// its name; keep it private. Empty string disables ntfy entirely.
#define NTFY_TOPIC "shunya-ujjain-3fae3f917736f9dddf13b258"
constexpr uint32_t NTFY_PERIOD_MS=10UL*60UL*1000UL;    // routine status
constexpr uint32_t NTFY_ALERT_GAP_MS=5UL*60UL*1000UL;  // minimum spacing of new-fault alerts
// Idle memory below these levels risks TLS (OTA / ntfy) failures.
constexpr uint32_t LOW_HEAP_WARN=80000, LOW_BLOCK_WARN=32000;
#define DEFAULT_OTA_MANIFEST_URL "https://raw.githubusercontent.com/randomroot18/gchamb-pblc-c/main/firmware/ujjain/manifest.json"
// Ujjain: heater command moved to 4-ch relay CH3 (GPIO19). GPIO13 (old 1-ch relay) is spare, held OFF.
constexpr int PIN_HUM=26, PIN_MIST=25, PIN_SPARE=13, PIN_EXHAUST=27, PIN_HEAT=19;
constexpr int TFT_CS=14, TFT_DC=17, TFT_RST=16, TFT_SCK=18, TFT_MOSI=23, TFT_MISO=34;
constexpr uint32_t TFT_HZ=10000000, SENSOR_MS=1000, SENSOR_STALE_MS=2500;
constexpr uint32_t SENSOR_RETRY_MS=30000, WDT_SECONDS=15;
// 38 C leaves 3 C above maximum user target (35 C), but is NOT a product
// temperature guarantee. Commission overshoot with the actual thermal mass.
constexpr float HARD_TEMP_C=38, RECOVERY_TEMP_C=35, MIN_SENSOR_C=-10, MAX_SENSOR_C=60;
constexpr uint32_t HOT_RECOVERY_MS=120000, BOOT_GUARD_MS=120000;
constexpr uint32_t HUM_PAUSE_MS=60000, MANUAL_HUM_MS=600000;
constexpr uint32_t MAX_HEAT_MS=600000, PURGE_SETTLE_MS=30000;
constexpr float PURGE_DROP=5, PURGE_END_RH=85;
constexpr uint32_t ADVISORY_CLEAR_MS=300000, DISAGREE_MS=300000;
constexpr uint32_t HTTP_TIMEOUT_MS=8000, DOWNLOAD_STALL_MS=15000, DOWNLOAD_TOTAL_MS=300000;
constexpr uint32_t OTA_MIN_HEAP=70000, FIRST_OTA_MS=45000;
constexpr uint32_t AP_AFTER_MS=180000, AP_LINGER_MS=180000;
constexpr uint32_t MIST_STARVATION_MS=20UL*60UL*1000UL, HUM_STARVATION_MS=30UL*60UL*1000UL;
constexpr uint32_t SCHEMA=2, SYSTEM_SCHEMA=1; // System blob layout is unchanged.
