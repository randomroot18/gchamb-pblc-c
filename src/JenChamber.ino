// ============================================================================
// Shunya germination chamber controller - field bootstrap build
//
// Changes from the previous working build, in priority order:
//   1. Touchscreen support removed entirely (display is not touch capable).
//      GPIO13 is allocated to heater, GPIO35 remains free. Do not reuse without a pin review.
//   2. Runtime sensor health: stale readings can no longer drive control.
//   3. Relay outputs are the first thing initialised in setup().
//   4. Persisted device modes are validated; misting can never latch ON.
//   5. Manual humidifier ON is bounded by MANUAL_HUMIDIFIER_MAX_RUNTIME_MS.
//   6. Firmware version, build id, hardware device id, friendly name.
//   7. Wi-Fi STA with NVS credentials, non-blocking state machine, backoff.
//   8. Provisioning AP only when there are no credentials, on request, or
//      after sustained STA failure. Not a permanent AP.
//   9. Pull-based HTTPS OTA: manifest -> version compare -> download to the
//      inactive slot -> SHA-256 verify -> reboot. Runs in its own task so the
//      control loop is never blocked. TLS is validated against embedded roots.
//  10. Task watchdog on the control loop.
//  11. NTP when online. Never required for control.
//
// Jen profile removes recirculation and adds a heater policy, disabled for bench tests.
// ============================================================================

#include <Wire.h>
#include <new>
#include "HeaterPolicy.h"
#if __has_include("telemetry_private.h")
#include "telemetry_private.h"
#else
#define JEN_TELEMETRY_URL ""
#define JEN_TELEMETRY_TOKEN ""
#endif
#include "Adafruit_SHT4x.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <uri/UriBraces.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_arduino_version.h>
#include <time.h>
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"

// ============================================================================
// FIRMWARE IDENTITY
// ============================================================================
#define FW_VERSION          "0.2.0-jen-bench"
#define FW_DEVICE_FAMILY    "germination-chamber-jen-v1"
#define FW_BUILD_ID         __DATE__ " " __TIME__
#define DEFAULT_DEVICE_NAME "SHUNYA-JEN-001"

// ============================================================================
// SAFETY / RELIABILITY CONSTANTS  (named on purpose, never buried in logic)
// ============================================================================
const float    MIN_VALID_TEMP_C = -10.0f;
const float    MAX_VALID_TEMP_C =  60.0f;
const uint8_t  SENSOR_FAIL_THRESHOLD     = 3;        // consecutive bad reads -> unhealthy
const uint32_t SENSOR_REINIT_INTERVAL_MS = 30000;    // retry a dead sensor this often
const uint32_t MANUAL_HUMIDIFIER_MAX_RUNTIME_MS = 30UL * 60UL * 1000UL;  // then back to AUTO
const uint32_t WDT_TIMEOUT_S = 15;                   // loop must feed within this

// ============================================================================
// NETWORK / OTA CONSTANTS
// ============================================================================
const uint32_t STA_CONNECT_TIMEOUT_MS      = 20000;
const uint32_t STA_RETRY_MIN_MS            = 5000;
const uint32_t STA_RETRY_MAX_MS            = 60000;
const uint32_t PROVISION_AFTER_STA_FAIL_MS = 10UL * 60UL * 1000UL;  // sustained failure -> raise AP as well
const uint32_t AP_LINGER_AFTER_STA_MS      = 3UL * 60UL * 1000UL;   // keep AP briefly once STA is up
const uint32_t OTA_FIRST_CHECK_DELAY_MS    = 45000;
const uint32_t OTA_DEFAULT_INTERVAL_MIN    = 720;     // 12 h. Shorten for tests: POST /api/config?ota_min=2
const uint32_t OTA_HTTP_TIMEOUT_MS         = 10000;
const uint32_t OTA_STALL_TIMEOUT_MS        = 20000;
const uint32_t OTA_MIN_FREE_HEAP           = 60000;   // TLS needs headroom
// Compile-time default manifest URL. Runtime override persists via /api/config?ota_url=
#define DEFAULT_OTA_MANIFEST_URL ""
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const char* TZ_INFO      = "IST-5:30";   // local display only; telemetry must use UTC epoch

// ============================================================================
// TLS ROOT CERTIFICATES
// Roots, not leaf or intermediate certs, so ordinary server cert rotation does
// not break OTA. Covers Let's Encrypt (ISRG Root X1), DigiCert (GitHub assets,
// many CDNs) and Sectigo/USERTrust (github.com, GitHub Pages at time of writing).
// Extracted from the Mozilla CA bundle via certifi. Verify what your host
// actually chains to with:
//   openssl s_client -connect <host>:443 -servername <host> </dev/null 2>/dev/null | grep -i "i:"
// Updating this bundle is itself an OTA. Expiry: ISRG 2035, DigiCert G2 2038,
// USERTrust 2038.
// ============================================================================
static const char ROOT_CA_BUNDLE[] PROGMEM =
  // ISRG Root X1
  "-----BEGIN CERTIFICATE-----\n"
  "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
  "TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
  "cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
  "WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
  "ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
  "MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
  "h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
  "0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
  "A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
  "T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
  "B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
  "B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
  "KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
  "OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
  "jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
  "qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
  "rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
  "HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
  "hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
  "ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
  "3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
  "NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
  "ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
  "TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
  "jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
  "oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
  "4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
  "mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
  "emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
  "-----END CERTIFICATE-----\n"
  // DigiCert Global Root G2
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
  "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
  "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
  "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
  "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
  "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
  "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
  "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
  "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
  "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
  "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
  "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
  "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
  "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
  "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
  "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
  "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
  "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
  "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
  "MrY=\n"
  "-----END CERTIFICATE-----\n"
  // USERTrust RSA Certification Authority
  "-----BEGIN CERTIFICATE-----\n"
  "MIIF3jCCA8agAwIBAgIQAf1tMPyjylGoG7xkDjUDLTANBgkqhkiG9w0BAQwFADCB\n"
  "iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\n"
  "cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\n"
  "BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAw\n"
  "MjAxMDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNV\n"
  "BAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVU\n"
  "aGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBSU0EgQ2Vy\n"
  "dGlmaWNhdGlvbiBBdXRob3JpdHkwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIK\n"
  "AoICAQCAEmUXNg7D2wiz0KxXDXbtzSfTTK1Qg2HiqiBNCS1kCdzOiZ/MPans9s/B\n"
  "3PHTsdZ7NygRK0faOca8Ohm0X6a9fZ2jY0K2dvKpOyuR+OJv0OwWIJAJPuLodMkY\n"
  "tJHUYmTbf6MG8YgYapAiPLz+E/CHFHv25B+O1ORRxhFnRghRy4YUVD+8M/5+bJz/\n"
  "Fp0YvVGONaanZshyZ9shZrHUm3gDwFA66Mzw3LyeTP6vBZY1H1dat//O+T23LLb2\n"
  "VN3I5xI6Ta5MirdcmrS3ID3KfyI0rn47aGYBROcBTkZTmzNg95S+UzeQc0PzMsNT\n"
  "79uq/nROacdrjGCT3sTHDN/hMq7MkztReJVni+49Vv4M0GkPGw/zJSZrM233bkf6\n"
  "c0Plfg6lZrEpfDKEY1WJxA3Bk1QwGROs0303p+tdOmw1XNtB1xLaqUkL39iAigmT\n"
  "Yo61Zs8liM2EuLE/pDkP2QKe6xJMlXzzawWpXhaDzLhn4ugTncxbgtNMs+1b/97l\n"
  "c6wjOy0AvzVVdAlJ2ElYGn+SNuZRkg7zJn0cTRe8yexDJtC/QV9AqURE9JnnV4ee\n"
  "UB9XVKg+/XRjL7FQZQnmWEIuQxpMtPAlR1n6BB6T1CZGSlCBst6+eLf8ZxXhyVeE\n"
  "Hg9j1uliutZfVS7qXMYoCAQlObgOK6nyTJccBz8NUvXt7y+CDwIDAQABo0IwQDAd\n"
  "BgNVHQ4EFgQUU3m/WqorSs9UgOHYm8Cd8rIDZsswDgYDVR0PAQH/BAQDAgEGMA8G\n"
  "A1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEMBQADggIBAFzUfA3P9wF9QZllDHPF\n"
  "Up/L+M+ZBn8b2kMVn54CVVeWFPFSPCeHlCjtHzoBN6J2/FNQwISbxmtOuowhT6KO\n"
  "VWKR82kV2LyI48SqC/3vqOlLVSoGIG1VeCkZ7l8wXEskEVX/JJpuXior7gtNn3/3\n"
  "ATiUFJVDBwn7YKnuHKsSjKCaXqeYalltiz8I+8jRRa8YFWSQEg9zKC7F4iRO/Fjs\n"
  "8PRF/iKz6y+O0tlFYQXBl2+odnKPi4w2r78NBc5xjeambx9spnFixdjQg3IM8WcR\n"
  "iQycE0xyNN+81XHfqnHd4blsjDwSXWXavVcStkNr/+XeTWYRUc+ZruwXtuhxkYze\n"
  "Sf7dNXGiFSeUHM9h4ya7b6NnJSFd5t0dCy5oGzuCr+yDZ4XUmFF0sbmZgIn/f3gZ\n"
  "XHlKYC6SQK5MNyosycdiyA5d9zZbyuAlJQG03RoHnHcAP9Dc1ew91Pq7P8yF1m9/\n"
  "qS3fuQL39ZeatTXaw2ewh0qpKJ4jjv9cJ2vhsE/zB+4ALtRZh8tSQZXq9EfX7mRB\n"
  "VXyNWQKV3WKdwrnuWih0hKWbt5DHDAff9Yk2dDLWKMGwsAvgnEzDHNb842m1R0aB\n"
  "L6KCq9NjRHDEjf8tM7qtj3u1cIiuPhnPQCjY/MiQu12ZIvVS5ljFH4gxQ+6IHdfG\n"
  "jjxDah2nGN59PRbxYvnKkKj9\n"
  "-----END CERTIFICATE-----\n"
  // USERTrust ECC Certification Authority
  "-----BEGIN CERTIFICATE-----\n"
  "MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n"
  "MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n"
  "eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n"
  "JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n"
  "MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n"
  "Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n"
  "VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n"
  "aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n"
  "I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n"
  "o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n"
  "A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n"
  "VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n"
  "zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n"
  "RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n"
  "-----END CERTIFICATE-----\n";


// ============================================================================
// SETPOINTS & ALGORITHM PARAMETERS
// ============================================================================
float humidity_target    = 85.0;
float humidity_band      = 5.0;
float temperature_target = 22.0;
float temperature_band   = 2.0;

// Fresh Air Exchange (FAE) — periodic exhaust runs for gas exchange even when
// humidity is fine. Closed chambers need this regardless of RH.
float fae_period_min     = 60;    // minutes between cycles (0 disables)
float fae_duration_sec   = 180;   // seconds each cycle runs

// There is no recirculation fan in this chamber. GPIO19 remains physically OFF.

// Misting / surface wetting — replaces heater on GPIO25 for now.
// This is for a pump/solenoid/nozzle that briefly wets maize seed trays.
float mist_period_min      = 180;   // minutes between wetting cycles; 0 disables
float mist_duration_sec    = 25;    // seconds per wetting cycle; start conservative

// RH disagreement blocks humidification until sensors are checked; no mixing fan exists.
float rh_balance_band = 2.0;      // max allowed %RH spread before humidifier is blocked
const float SENSOR_DISAGREE_T = 1.5;  // °C difference still triggers mixing

// ============================================================================
// HARDWARE PINS
// ============================================================================
// Relay outputs. UNCHANGED from the previous build. Do not remap until the
// exact module and relay board are confirmed on site.
#define PIN_HUM     26   // Humidifier (ultrasonic atomizer + driver)
#define PIN_MIST    25   // Misting pump/solenoid (former heater pin)
#define PIN_UNUSED_CH3 19 // IN3 wired but with no load: permanently OFF
#define PIN_HEATER  13   // Separate single relay switches 5 V to SSR input
#define PIN_EXHAUST 27   // Exhaust fan (external venting)
#define RELAY_ACTIVE_LOW true

// 2.8 inch 240x320 SPI TFT, ILI9341. No touch controller is installed.
#define TFT_CS    14
#define TFT_DC    17
#define TFT_RST   16
#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_MISO  34   // display SDO may stay disconnected; 34 is input-only so it is a harmless MISO

// GPIO13 is the dedicated heater command. GPIO35 remains unallocated.
// GPIO12 must stay free (MTDI strapping pin, selects flash voltage at boot).

enum DeviceMode { MODE_AUTO, MODE_ON, MODE_OFF };
enum DeviceKind { KIND_HUMIDIFIER, KIND_MISTING, KIND_HEATER, KIND_EXHAUST };

struct Device {
  const char* name;
  const char* kindStr;       // for JSON
  uint8_t pin;
  DeviceKind kind;
  DeviceMode mode;
  bool state;
};

Device devices[] = {
  { "Humidifier",         "humidifier", PIN_HUM,     KIND_HUMIDIFIER, MODE_AUTO, false },
  { "Misting",            "misting",    PIN_MIST,    KIND_MISTING,    MODE_AUTO, false },
  { "Heater",             "heater",     PIN_HEATER,  KIND_HEATER,     MODE_AUTO, false },
  { "Exhaust Fan",        "exhaust",    PIN_EXHAUST, KIND_EXHAUST,    MODE_AUTO, false },
};
const int NUM_DEVICES = sizeof(devices) / sizeof(devices[0]);

// Display order for the web UI (each entry is an index into devices[]).
// Persisted across reboots. Local TFT keeps a fixed kind-based order for muscle memory.
uint8_t deviceOrder[NUM_DEVICES] = {0, 1, 2, 3};

// ============================================================================
// SENSOR & SYSTEM STATE
// ============================================================================
// Runtime sensor health. A sensor that stops answering, or answers with
// garbage, is taken out of service after SENSOR_FAIL_THRESHOLD bad reads and
// retried every SENSOR_REINIT_INTERVAL_MS. Its values are set to NAN so a
// stale number can never drive control.
struct SensorHealth {
  bool     healthy;
  uint8_t  failStreak;
  uint32_t lastGoodMs;
  uint32_t lastReinitMs;
  uint32_t failCount;      // lifetime, diagnostics only
};
SensorHealth sh1 = {false, 0, 0, 0, 0};
SensorHealth sh2 = {false, 0, 0, 0, 0};
float s1_temp=NAN, s1_rh=NAN, s2_temp=NAN, s2_rh=NAN;
// s1_ok / s2_ok mean "healthy AND holding a validated reading". Every existing
// reducer, control and UI path keys off these, so they are kept as-is.
bool  s1_ok=false, s2_ok=false;

// Manual humidifier bounding
unsigned long humManualOnAt   = 0;
bool          humManualTimedOut = false;
bool          settingsDirty   = false;   // deferred NVS write from inside control

// Identity
char   deviceId[13] = "000000000000";   // 12 hex chars from the eFuse base MAC
String deviceName;

// Wi-Fi state machine
enum WifiState { WS_NO_CREDS, WS_CONNECTING, WS_CONNECTED, WS_WAIT_RETRY };
WifiState     wifiState      = WS_NO_CREDS;
String        wifiSsid, wifiPass;
unsigned long wifiStateAt    = 0;
unsigned long wifiRetryMs    = STA_RETRY_MIN_MS;
unsigned long staDownSince   = 0;
unsigned long staConnectedAt = 0;
bool          apActive       = false;
unsigned long apStartedAt    = 0;
bool          apRequested    = false;
bool          ntpStarted     = false;

// OTA
String        otaManifestUrl;
uint32_t      otaIntervalMin = OTA_DEFAULT_INTERVAL_MIN;
volatile bool otaBusy          = false;
volatile bool otaRebootPending = false;
bool          otaForceFlag     = false;
unsigned long otaNextCheckAt   = 0;
unsigned long otaLastCheckMs   = 0;
uint32_t      otaChecks        = 0;
char          otaResult[96]    = "never";
char          otaAvailable[24] = "";
portMUX_TYPE  otaMux = portMUX_INITIALIZER_UNLOCKED;

// Forward declarations for functions used before their definition. The Arduino
// IDE normally generates these, but being explicit costs nothing.
String apSsid();
String apPass();
String currentIp();
void   startAP();
void   staBegin();
void   otaRequestCheck(bool force);
unsigned long lastReading = 0;
unsigned long lastDisplayRefresh = 0;

// Keep sensor/control updates conservative, but refresh the TFT faster so
// countdowns, status changes, and touch feedback feel more responsive.
const unsigned long SENSOR_REFRESH_MS  = 1000;  // SHT readings + control loop
const unsigned long DISPLAY_REFRESH_MS = 120;   // TFT change-gated redraw poll (~8 Hz)

// Algorithm runtime state
bool          faeRunning           = false;
unsigned long faeChangedAt         = 0;
bool          mistCycleRunning     = false;
unsigned long mistChangedAt        = 0;
bool          mistManualRunning    = false;
unsigned long mistManualStartedAt  = 0;
bool          sensorsDisagree      = false;
bool          rhBalanceHold        = false; // sensor disagreement: humidifier blocked
float         rhSensorSpread       = -1;    // absolute difference between S1 and S2 RH
bool          exhaustPurging       = false;
unsigned long lastPurgeEndAt       = 0;
float         purge_cooldown_min   = 0;   // min minutes between purges; 0 = pure reactive valve

// Bench build: compile-time lock keeps the mains heater OFF until thermal data,
// independent high-limit, and commissioning checks have been completed.
constexpr bool HEATER_COMMISSIONED = false;
heater::Config heaterConfig;
heater::State heaterState;
Adafruit_SHT4x sensor1, sensor2;
TwoWire I2C_two = TwoWire(1);
SPIClass TFTSPI(VSPI);
Adafruit_ILI9341 tft(&TFTSPI, TFT_DC, TFT_CS, TFT_RST);
WebServer server(80);
Preferences prefs;
volatile bool telemetryBusy = false;
uint32_t telemetryLastAt = 0;
const uint32_t TELEMETRY_INTERVAL_MS = 30UL * 1000UL;
struct TelemetryJob { char body[640]; };

// Network work runs on another core. The control loop copies a small snapshot
// first, then never waits for DNS, TLS, or the collector.
void telemetryTask(void* context) {
  TelemetryJob* job = static_cast<TelemetryJob*>(context);
  WiFiClientSecure client;
  client.setCACert(ROOT_CA_BUNDLE);
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (http.begin(client, JEN_TELEMETRY_URL)) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + JEN_TELEMETRY_TOKEN);
    int result = http.POST((uint8_t*)job->body, strlen(job->body));
    if (result != 204) Serial.printf("[telemetry] collector HTTP %d\n", result);
    http.end();
  }
  delete job;
  telemetryBusy = false;
  vTaskDelete(NULL);
}

void serviceTelemetry(uint32_t now) {
  if (!JEN_TELEMETRY_URL[0] || !JEN_TELEMETRY_TOKEN[0] || telemetryBusy ||
      wifiState != WS_CONNECTED || time(nullptr) < 1700000000L ||
      (uint32_t)(now - telemetryLastAt) < TELEMETRY_INTERVAL_MS) return;
  telemetryLastAt = now;
  TelemetryJob* job = new (std::nothrow) TelemetryJob;
  if (!job) return;
  snprintf(job->body, sizeof(job->body),
    "{\"device_id\":\"%s\",\"family\":\"%s\",\"fw\":\"%s\",\"epoch\":%ld,"
    "\"s1_ok\":%s,\"s1_t\":%.2f,\"s1_rh\":%.2f,"
    "\"s2_ok\":%s,\"s2_t\":%.2f,\"s2_rh\":%.2f,"
    "\"heater_on\":%s,\"heater_trip\":%s,\"heater_commissioned\":%s,"
    "\"hum_on\":%s,\"mist_on\":%s,\"exhaust_on\":%s,"
    "\"temp_target\":%.1f,\"hum_target\":%.1f,\"rssi\":%d}",
    deviceId, FW_DEVICE_FAMILY, FW_VERSION, (long)time(nullptr),
    s1_ok ? "true":"false", s1_ok ? s1_temp:0.0f, s1_ok ? s1_rh:0.0f,
    s2_ok ? "true":"false", s2_ok ? s2_temp:0.0f, s2_ok ? s2_rh:0.0f,
    devices[2].state ? "true":"false", heaterState.tripped ? "true":"false",
    HEATER_COMMISSIONED ? "true":"false",
    devices[0].state ? "true":"false", devices[1].state ? "true":"false",
    devices[3].state ? "true":"false", temperature_target, humidity_target,
    WiFi.RSSI());
  telemetryBusy = true;
  if (xTaskCreatePinnedToCore(telemetryTask,"telemetry",12288,job,1,NULL,0) != pdPASS) {
    telemetryBusy = false;
    delete job;
  }
}


// ============================================================================
// HELPERS
// ============================================================================
void setRelay(int pin, bool on) {
  digitalWrite(pin, (RELAY_ACTIVE_LOW ? !on : on) ? HIGH : LOW);
}

// Drive every relay to its OFF level and set the pins as outputs. Called as the
// very first thing in setup() and again immediately before any reboot.
//
// LIMIT OF WHAT SOFTWARE CAN DO: between power-on / reset and the first line of
// setup(), the ROM bootloader and second-stage bootloader run for tens of
// milliseconds with these GPIOs in a high-impedance state. On an active-LOW
// relay input a floating pin can read as "ON". Production hardware must bias
// every relay input to its OFF (HIGH) state electrically, independent of the
// ESP32. The correct component depends on the relay board's input circuit
// (bare transistor, opto, driver IC), so inspect the board before choosing.
void allRelaysOff() {
  digitalWrite(PIN_UNUSED_CH3, HIGH);
  pinMode(PIN_UNUSED_CH3, OUTPUT);
  for (int i = 0; i < NUM_DEVICES; i++) {
    digitalWrite(devices[i].pin, HIGH); // preload OFF before output enable
    pinMode(devices[i].pin, OUTPUT);
    devices[i].state = false;
  }
  heaterState.on = false;
}

int findIdx(DeviceKind k) {
  for (int i = 0; i < NUM_DEVICES; i++) if (devices[i].kind == k) return i;
  return -1;
}

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void startMistPulse() {
  if (mist_duration_sec <= 0) return;
  mistManualRunning = true;
  mistManualStartedAt = millis();
}

unsigned long mistRemainingSec() {
  unsigned long now = millis();
  unsigned long durMs = (unsigned long)(mist_duration_sec * 1000.0f);
  if (mistManualRunning && now - mistManualStartedAt < durMs) return (durMs - (now - mistManualStartedAt)) / 1000UL;
  if (mistCycleRunning  && now - mistChangedAt       < durMs) return (durMs - (now - mistChangedAt))       / 1000UL;
  return 0;
}

unsigned long mistNextSec() {
  if (mist_period_min <= 0 || mist_duration_sec <= 0 || mistCycleRunning || mistManualRunning) return 0;
  unsigned long now = millis();
  unsigned long periodMs = (unsigned long)(mist_period_min * 60000.0f);
  unsigned long elapsed = now - mistChangedAt;
  if (elapsed >= periodMs) return 0;
  return (periodMs - elapsed) / 1000UL;
}

// ============================================================================
// SENSOR REDUCERS — pick the right sensor for each control loop
// ============================================================================

// Driest spot drives the humidifier (humidify until even the driest is up)
float controlRH_low() {
  if (s1_ok && s2_ok) return min(s1_rh, s2_rh);
  if (s1_ok) return s1_rh;
  if (s2_ok) return s2_rh;
  return -1;
}

// Wettest spot drives the exhaust (vent if even one spot is too wet)
float controlRH_high() {
  if (s1_ok && s2_ok) return max(s1_rh, s2_rh);
  if (s1_ok) return s1_rh;
  if (s2_ok) return s2_rh;
  return -1;
}

// Coldest temperature reducer for monitoring/readout; heater checks BOTH sensors.
float controlTemp_low() {
  if (s1_ok && s2_ok) return min(s1_temp, s2_temp);
  if (s1_ok) return s1_temp;
  if (s2_ok) return s2_temp;
  return -999;
}

// ============================================================================
// CONTROL ALGORITHM
// ============================================================================
//
// Priority order matters because some devices interlock:
//   1. EXHAUST: over-humidity venting OR scheduled FAE cycle
//   2. HUMIDIFIER: standard hysteresis, BUT paused while exhaust is venting
//   3. MISTING:   timed surface wetting cycle on the former heater relay pin
//   4. HEATER: dedicated single relay, fail closed and disabled for bench work
//
void updateRelays() {
  unsigned long now = millis();
  const float hLow  = humidity_target - humidity_band / 2;
  const float hHigh = humidity_target + humidity_band / 2;

  int iHum     = findIdx(KIND_HUMIDIFIER);
  int iMist    = findIdx(KIND_MISTING);
  int iHeater  = findIdx(KIND_HEATER);
  int iExhaust = findIdx(KIND_EXHAUST);

  // ---- FAE state machine ----
  if (fae_period_min > 0 && fae_duration_sec > 0) {
    if (faeRunning) {
      if (now - faeChangedAt >= (unsigned long)(fae_duration_sec * 1000.0f)) {
        faeRunning = false;
        faeChangedAt = now;
      }
    } else {
      if (now - faeChangedAt >= (unsigned long)(fae_period_min * 60000.0f)) {
        faeRunning = true;
        faeChangedAt = now;
      }
    }
  } else {
    faeRunning = false;
  }

  // ---- Misting periodic-cycle + manual pulse state machines ----
  if (mistManualRunning) {
    if (now - mistManualStartedAt >= (unsigned long)(mist_duration_sec * 1000.0f)) {
      mistManualRunning = false;
    }
  }

  if (mist_period_min > 0 && mist_duration_sec > 0) {
    if (mistCycleRunning) {
      if (now - mistChangedAt >= (unsigned long)(mist_duration_sec * 1000.0f)) {
        mistCycleRunning = false;
        mistChangedAt = now;
      }
    } else {
      if (now - mistChangedAt >= (unsigned long)(mist_period_min * 60000.0f)) {
        mistCycleRunning = true;
        mistChangedAt = now;
      }
    }
  } else {
    mistCycleRunning = false;
  }

  // ---- Sensor balance gate: pause humidification on disagreement ----
  sensorsDisagree = false;
  rhBalanceHold = false;
  rhSensorSpread = -1;
  if (s1_ok && s2_ok) {
    rhSensorSpread = fabsf(s1_rh - s2_rh);
    rhBalanceHold = (rh_balance_band > 0) && (rhSensorSpread > rh_balance_band);
    if (rhBalanceHold) sensorsDisagree = true;
    if (fabsf(s1_temp - s2_temp) > SENSOR_DISAGREE_T) sensorsDisagree = true;
  }

  float rh_low = controlRH_low();   // driest sensor drives humidifier

  // ---- EXHAUST (relief valve: opens at hHigh, vents back to target, then cooldown) ----
  bool autoExhaust = false;
  if (rh_low >= 0) {
    if (exhaustPurging) {
      if (rh_low <= humidity_target) {            // back to target -> close, start cooldown
        exhaustPurging = false;
        lastPurgeEndAt = now;
      }
    } else {
      bool cooldownOver =
          (purge_cooldown_min <= 0) ||
          (now - lastPurgeEndAt >= (unsigned long)(purge_cooldown_min * 60000.0f));
      if (rh_low > hHigh && cooldownOver) {        // climbed past upper edge -> open
        exhaustPurging = true;
      }
    }
  }
  autoExhaust = exhaustPurging || faeRunning;

  // ---- HUMIDIFIER (air RH control; paused while exhausting or while sensors need mixing) ----
  bool autoHum = false;
  if (rh_low >= 0 && !autoExhaust && !rhBalanceHold) {
    if      (rh_low < hLow)             autoHum = true;
    else if (rh_low >= humidity_target) autoHum = false;
    else                                autoHum = (iHum >= 0) ? devices[iHum].state : false;
  }

  // ---- MISTING (surface wetting; sprays no matter exhaust state) ----
  bool autoMist = (s1_ok || s2_ok) && (mistCycleRunning || mistManualRunning);

  // Dedicated heater relay -> SSR. A bad/missing sensor, excessive disagreement,
  // hard temperature limit or disabled commissioning forces the relay OFF.
  heaterConfig.targetC = temperature_target;
  bool autoHeat = heater::step(heaterState, heaterConfig, now,
                               s1_ok, s1_temp, s2_ok, s2_temp,
                               HEATER_COMMISSIONED);

  // ---- Manual-output bounding (safety, independent of the algorithm above) ----
  // Misting: MODE_ON is never a valid persistent state. The API converts ON
  // into a timed pulse; this catches anything that slipped past that.
  if (iMist >= 0 && devices[iMist].mode == MODE_ON) {
    devices[iMist].mode = MODE_AUTO;
    settingsDirty = true;
  }
  // Humidifier: manual ON is allowed but bounded. After the limit it returns
  // to AUTO so the hysteresis loop takes over again.
  if (iHum >= 0 && devices[iHum].mode == MODE_ON &&
      now - humManualOnAt >= MANUAL_HUMIDIFIER_MAX_RUNTIME_MS) {
    devices[iHum].mode = MODE_AUTO;
    humManualTimedOut = true;
    settingsDirty = true;
    Serial.println("[safety] manual humidifier runtime limit reached, reverting to AUTO");
  }

  // ---- Apply with per-device mode override ----
  for (int i = 0; i < NUM_DEVICES; i++) {
    bool autoT = false;
    if      (i == iHum)     autoT = autoHum;
    else if (i == iMist)    autoT = autoMist;
    else if (i == iHeater)  autoT = autoHeat;
    else if (i == iExhaust) autoT = autoExhaust;

    bool target;
    if (i == iHeater) target = autoHeat && devices[i].mode != MODE_OFF;
    else if ((!s1_ok && !s2_ok) && (i == iHum || i == iMist)) target = false;
    else if (devices[i].mode == MODE_ON) target = true;
    else if (devices[i].mode == MODE_OFF) target = false;
    else target = autoT;

    if (target != devices[i].state) {
      devices[i].state = target;
      setRelay(devices[i].pin, target);
    }
  }
}

// A successful I2C transaction is not a valid reading. Reject NaN, inf,
// impossible RH, and temperatures outside the chamber-plausible window.
// Never clamp: a bad reading is discarded, not repaired.
static bool readingValid(float t, float rh) {
  if (!isfinite(t) || !isfinite(rh)) return false;
  if (rh < 0.0f || rh > 100.0f) return false;
  if (t < MIN_VALID_TEMP_C || t > MAX_VALID_TEMP_C) return false;
  return true;
}

// Service one sensor: read and validate if healthy, count failures, take it out
// of service after the threshold, and periodically try to bring it back.
static void serviceSensor(Adafruit_SHT4x& dev, TwoWire& bus, SensorHealth& h,
                          float& tOut, float& rhOut, bool& okOut, const char* tag) {
  unsigned long now = millis();

  if (!h.healthy) {
    if (now - h.lastReinitMs < SENSOR_REINIT_INTERVAL_MS) { okOut = false; return; }
    h.lastReinitMs = now;
    if (!dev.begin(&bus)) { okOut = false; return; }   // still absent
    // begin() answered. Fall through and demand one valid reading before trusting it.
  }

  sensors_event_t hev, tev;
  bool ok = dev.getEvent(&hev, &tev) && readingValid(tev.temperature, hev.relative_humidity);

  if (ok) {
    tOut = tev.temperature;
    rhOut = hev.relative_humidity;
    h.lastGoodMs = now;
    h.failStreak = 0;
    if (!h.healthy) {
      h.healthy = true;
      Serial.printf("[sensor] %s recovered\n", tag);
    }
  } else {
    h.failCount++;
    if (h.healthy) {
      if (++h.failStreak >= SENSOR_FAIL_THRESHOLD) {
        h.healthy = false;
        h.lastReinitMs = now;
        tOut = NAN; rhOut = NAN;   // stale values must never participate in control
        Serial.printf("[sensor] %s UNHEALTHY after %u consecutive bad reads\n", tag, h.failStreak);
      }
      // below threshold: keep the last good value (at most a few seconds old)
    } else {
      tOut = NAN; rhOut = NAN;
    }
  }
  okOut = h.healthy && isfinite(tOut) && isfinite(rhOut);
}

void readSensors() {
  serviceSensor(sensor1, Wire,    sh1, s1_temp, s1_rh, s1_ok, "S1");
  serviceSensor(sensor2, I2C_two, sh2, s2_temp, s2_rh, s2_ok, "S2");
}

// ============================================================================
// PERSISTENCE
// ============================================================================
void saveSettings() {
  prefs.begin("climate", false);
  prefs.putFloat("ht", humidity_target);
  prefs.putFloat("hb", humidity_band);
  prefs.putFloat("tt", temperature_target);
  prefs.putFloat("tb", temperature_band);
  prefs.putFloat("fp", fae_period_min);
  prefs.putFloat("fd", fae_duration_sec);
  prefs.putFloat("mp", mist_period_min);
  prefs.putFloat("md", mist_duration_sec);
  prefs.putFloat("rbb", rh_balance_band);
  prefs.putFloat("pcd", purge_cooldown_min);
  for (int i = 0; i < NUM_DEVICES; i++) {
    char key[6]; snprintf(key, sizeof(key), "m%d", i);
    prefs.putUChar(key, (uint8_t)devices[i].mode);
  }
  prefs.putBytes("order", deviceOrder, NUM_DEVICES);
  prefs.end();
}

void loadSettings() {
  prefs.begin("climate", true);
  humidity_target     = prefs.getFloat("ht", humidity_target);
  humidity_band       = prefs.getFloat("hb", humidity_band);
  temperature_target  = prefs.getFloat("tt", temperature_target);
  temperature_band    = prefs.getFloat("tb", temperature_band);
  fae_period_min      = prefs.getFloat("fp", fae_period_min);
  fae_duration_sec    = prefs.getFloat("fd", fae_duration_sec);
  mist_period_min     = prefs.getFloat("mp", mist_period_min);
  mist_duration_sec   = prefs.getFloat("md", mist_duration_sec);
  rh_balance_band     = prefs.getFloat("rbb", rh_balance_band);
  purge_cooldown_min  = prefs.getFloat("pcd", purge_cooldown_min);
  for (int i = 0; i < NUM_DEVICES; i++) {
    char key[6]; snprintf(key, sizeof(key), "m%d", i);
    uint8_t m = prefs.getUChar(key, (uint8_t)MODE_AUTO);
    // Validate: only the three known modes are accepted, anything else is AUTO.
    if (m > (uint8_t)MODE_OFF) m = (uint8_t)MODE_AUTO;
    // Misting must never come up latched ON from old NVS. ON means "pulse" for
    // misting and is never a persistent state.
    if ((devices[i].kind == KIND_MISTING || devices[i].kind == KIND_HEATER) && m == (uint8_t)MODE_ON) m = (uint8_t)MODE_AUTO;
    devices[i].mode = (DeviceMode)m;
    // A humidifier restored as ON starts its bounded-runtime clock at boot.
    if (devices[i].kind == KIND_HUMIDIFIER && m == (uint8_t)MODE_ON) humManualOnAt = millis();
  }
  uint8_t tmp[NUM_DEVICES];
  size_t got = prefs.getBytes("order", tmp, NUM_DEVICES);
  prefs.end();

  // Validate stored order before adopting — discard if corrupt or first boot
  if (got == NUM_DEVICES) {
    bool seen[NUM_DEVICES] = {false};
    bool valid = true;
    for (int i = 0; i < NUM_DEVICES && valid; i++) {
      if (tmp[i] >= NUM_DEVICES || seen[tmp[i]]) { valid = false; break; }
      seen[tmp[i]] = true;
    }
    if (valid) memcpy(deviceOrder, tmp, NUM_DEVICES);
  }
}

// System-level settings live in their own namespace so the existing "climate"
// namespace and its keys are untouched.
void loadSystem() {
  prefs.begin("sys", true);
  deviceName     = prefs.getString("name", DEFAULT_DEVICE_NAME);
  wifiSsid       = prefs.getString("ssid", "");
  wifiPass       = prefs.getString("pass", "");
  otaManifestUrl = prefs.getString("otaUrl", DEFAULT_OTA_MANIFEST_URL);
  otaIntervalMin = prefs.getUInt("otaMin", OTA_DEFAULT_INTERVAL_MIN);
  prefs.end();
  if (otaIntervalMin < 1) otaIntervalMin = 1;
}

void saveSystem() {
  prefs.begin("sys", false);
  prefs.putString("name", deviceName);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putString("otaUrl", otaManifestUrl);
  prefs.putUInt("otaMin", otaIntervalMin);
  prefs.end();
}

void initDeviceId() {
  uint64_t mac = ESP.getEfuseMac();   // base MAC, bytes 0..5 in transmission order
  snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
           (unsigned)(mac & 0xFF), (unsigned)((mac >> 8) & 0xFF), (unsigned)((mac >> 16) & 0xFF),
           (unsigned)((mac >> 24) & 0xFF), (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
}

// ============================================================================
// TFT DISPLAY  (portrait 240x320, flicker-free change-gated rendering)
// ============================================================================
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// ---- palette (RGB565, matched to the web UI) ----
#define C_BG     0x0000   // near-black background
#define C_CARD   0x10A2   // #161616 card fill
#define C_BORDER 0x2945   // subtle card edge
#define C_TX     0xFFFF   // primary text
#define C_DIM    0x8C51   // #888 labels
#define C_FAINT  0x52AA   // quieter labels
#define C_ACCENT 0x3C1E   // #3b82f6 blue
#define C_ON     0x2DEC   // green
#define C_OFF    0x39C7   // #3a3a3a inactive
#define C_BAD    0xE30D   // #e0626f offline/alert
#define C_AMBER  0xFCC0   // misting
#define C_BTN    0x2104   // tappable chip fill, visible on black

// ---- layout (240 x 320) ----
#define HERO_X 6
#define HERO_Y 32
#define HERO_W 228
#define HERO_H 108

#define SEN_Y  146
#define SEN_H  54
#define SEN1_X 6
#define SEN2_X 123
#define SEN_W  111

#define DEV_X  6
#define DEV_W  228
#define DEV_Y  206
#define DEV_H  108
#define DEV_ROWH (DEV_H / NUM_DEVICES)

// ---- per-field caches (sentinels force a draw on first pass / full redraw) ----
// All text caches are 20 bytes so diff() can keep 19 characters. The previous
// 13-character truncation meant a 15-character IP never matched its cache and
// that cell redrew on every pass.
#define UI_CACHE_LEN 20
struct UiCache {
  char     ip[UI_CACHE_LEN], status[UI_CACHE_LEN], heroRH[UI_CACHE_LEN], target[UI_CACHE_LEN], spread[UI_CACHE_LEN];
  char     s1t[UI_CACHE_LEN], s1rh[UI_CACHE_LEN], s2t[UI_CACHE_LEN], s2rh[UI_CACHE_LEN];
  int      barCur, barTgt;
  int8_t   devOn[NUM_DEVICES];
  char     devState[NUM_DEVICES][UI_CACHE_LEN];
  uint16_t linkDot;
} ui;

void resetUiCache() {
  const char inv[2] = {'\x01', 0};
  strcpy(ui.ip, inv);   strcpy(ui.status, inv); strcpy(ui.heroRH, inv);
  strcpy(ui.target, inv); strcpy(ui.spread, inv);
  strcpy(ui.s1t, inv);  strcpy(ui.s1rh, inv);  strcpy(ui.s2t, inv);  strcpy(ui.s2rh, inv);
  ui.barCur = -32768; ui.barTgt = -32768;
  for (int i = 0; i < NUM_DEVICES; i++) { ui.devOn[i] = -1; ui.devState[i][0] = '\x01'; ui.devState[i][1] = 0; }
  ui.linkDot = 0xFFFF;
}

static bool diff(char* cache, const char* s) {
  if (strcmp(cache, s) == 0) return false;
  strncpy(cache, s, UI_CACHE_LEN - 1); cache[UI_CACHE_LEN - 1] = 0;
  return true;
}

uint16_t kindColor(DeviceKind k) {
  switch (k) {
    case KIND_HUMIDIFIER: return C_ON;
    case KIND_MISTING:    return C_AMBER;
    case KIND_HEATER:     return C_BAD;
    case KIND_EXHAUST:    return C_BAD;
  }
  return C_ON;
}

// Fill a fixed cell with its background, then draw text inside it.
// align: 0 left, 1 center, 2 right. font = NULL uses the built-in 5x7 font.
void drawTextCell(int x, int y, int w, int h, const char* s,
                  const GFXfont* font, uint16_t fg, uint16_t bg, uint8_t align) {
  tft.fillRect(x, y, w, h, bg);
  tft.setFont(font);
  tft.setTextSize(1);
  tft.setTextColor(fg);
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  int tx;
  if      (align == 1) tx = x + (w - (int)bw) / 2 - bx;
  else if (align == 2) tx = x + w - (int)bw - bx - 2;
  else                 tx = x - bx + 2;
  int ty = y + (h - (int)bh) / 2 - by;
  tft.setCursor(tx, ty);
  tft.print(s);
  tft.setFont();
}

void drawCard(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 10, C_CARD);
  tft.drawRoundRect(x, y, w, h, 10, C_BORDER);
}

void drawTinyLabel(int x, int y, const char* s, uint16_t fg, uint16_t bg) {
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(fg, bg);
  tft.setCursor(x, y);
  tft.print(s);
}

// Left-aligned GFX text, vertically centred on cy. For static labels only.
void drawGfxLeft(int x, int cy, const char* s, const GFXfont* font, uint16_t fg) {
  tft.setFont(font);
  tft.setTextColor(fg);
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  tft.setCursor(x - bx, cy - bh / 2 - by);
  tft.print(s);
  tft.setFont();
}

// Humidity band bar: track, accent fill to current, bright tick at target.
void drawBand(int x, int y, int w, int h, float cur, float tgt) {
  const float lo = 40, hi = 100;
  tft.fillRoundRect(x, y, w, h, h / 2, C_OFF);
  if (cur >= 0) {
    float f = (clampf(cur, lo, hi) - lo) / (hi - lo);
    int fw = (int)(f * w);
    if (fw < h) fw = h;
    if (fw > w) fw = w;
    tft.fillRoundRect(x, y, fw, h, h / 2, C_ACCENT);
  }
  float tf = (clampf(tgt, lo, hi) - lo) / (hi - lo);
  int tx = x + (int)(tf * w);
  tft.fillRect(tx - 1, y - 2, 3, h + 4, C_TX);
}

void drawStaticDisplayFrame() {
  tft.fillScreen(C_BG);

  // header: link dot (dynamic, see drawDisplay), firmware version, IP/AP cell
  tft.fillCircle(12, 13, 4, C_DIM);
  drawTinyLabel(24, 8, FW_VERSION, C_TX, C_BG);
  tft.drawFastHLine(6, 28, 228, C_BORDER);

  // hero card + fixed labels
  drawCard(HERO_X, HERO_Y, HERO_W, HERO_H);
  drawTinyLabel(HERO_X + 12, HERO_Y + 12, "HUMIDITY", C_DIM, C_CARD);
  drawTinyLabel(HERO_X + 150, HERO_Y + 34, "TARGET", C_FAINT, C_CARD);
  drawTinyLabel(HERO_X + 150, HERO_Y + 74, "SPREAD", C_FAINT, C_CARD);

  // sensor cards + fixed labels
  drawCard(SEN1_X, SEN_Y, SEN_W, SEN_H);
  drawCard(SEN2_X, SEN_Y, SEN_W, SEN_H);
  drawTinyLabel(SEN1_X + 10, SEN_Y + 8, "SENSOR 1", C_DIM, C_CARD);
  drawTinyLabel(SEN2_X + 10, SEN_Y + 8, "SENSOR 2", C_DIM, C_CARD);

  // device card, separators, fixed names
  drawCard(DEV_X, DEV_Y, DEV_W, DEV_H);
  for (int i = 0; i < NUM_DEVICES; i++) {
    int ry = DEV_Y + i * DEV_ROWH;
    int cy = ry + DEV_ROWH / 2;
    if (i > 0) tft.drawFastHLine(DEV_X + 12, ry, DEV_W - 24, C_BORDER);
    drawGfxLeft(DEV_X + 28, cy, devices[i].name, &FreeSans9pt7b, C_TX);
  }
}

void drawDisplay(bool fullRedraw = false) {
  static bool frameDrawn = false;
  if (fullRedraw || !frameDrawn) {
    drawStaticDisplayFrame();
    frameDrawn = true;
    resetUiCache();
  }

  char buf[24];

  int iH = findIdx(KIND_HUMIDIFIER);
  float rh  = controlRH_low();
  bool humOn = (iH >= 0) && devices[iH].state;
  unsigned long mr = mistRemainingSec();

  // ---- header: link dot + address (STA IP, or AP name while provisioning) ----
  uint16_t dot = (wifiState == WS_CONNECTED) ? C_ON : (apActive ? C_AMBER : C_DIM);
  if (dot != ui.linkDot) { ui.linkDot = dot; tft.fillCircle(12, 13, 4, dot); }
  if      (wifiState == WS_CONNECTED) snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
  else if (apActive)                  snprintf(buf, sizeof(buf), "AP %s", apSsid().c_str());
  else                                snprintf(buf, sizeof(buf), "no wifi");
  if (diff(ui.ip, buf)) drawTextCell(118, 4, 116, 12, buf, NULL, C_DIM, C_BG, 2);

  // ---- hero status word ----
  const char* st; uint16_t sc;
  if      (exhaustPurging || faeRunning) { st = "VENTING";     sc = C_BAD; }
  else if (rhBalanceHold)                { st = "SENSOR CHECK"; sc = C_BAD; }
  else if (heaterState.on)               { st = "HEATING";     sc = C_AMBER; }
  else if (humOn)                        { st = "HUMIDIFYING"; sc = C_ON; }
  else if (mr > 0)                       { st = "MISTING";     sc = C_AMBER; }
  else                                   { st = "IDLE";        sc = C_DIM; }
  if (diff(ui.status, st))
    drawTextCell(HERO_X + 96, HERO_Y + 8, 126, 14, st, NULL, sc, C_CARD, 2);

  // ---- hero RH (big) ----
  if (rh >= 0) snprintf(buf, sizeof(buf), "%.0f%%", rh);
  else         snprintf(buf, sizeof(buf), "--%%");
  if (diff(ui.heroRH, buf))
    drawTextCell(HERO_X + 12, HERO_Y + 30, 132, 60, buf, &FreeSansBold24pt7b,
                 rh >= 0 ? C_TX : C_BAD, C_CARD, 0);

  // ---- target ----
  snprintf(buf, sizeof(buf), "%.0f%%", humidity_target);
  if (diff(ui.target, buf))
    drawTextCell(HERO_X + 150, HERO_Y + 44, 72, 24, buf, &FreeSansBold9pt7b, C_ACCENT, C_CARD, 0);

  // ---- spread ----
  if (rhSensorSpread >= 0) snprintf(buf, sizeof(buf), "%.1f%%", rhSensorSpread);
  else                     snprintf(buf, sizeof(buf), "--");
  if (diff(ui.spread, buf))
    drawTextCell(HERO_X + 150, HERO_Y + 84, 72, 22, buf, &FreeSans9pt7b,
                 rhBalanceHold ? C_BAD : C_DIM, C_CARD, 0);

  // ---- band bar (redraw only when current or target integer changes) ----
  int barCur = (rh >= 0) ? (int)lroundf(rh) : -1;
  int barTgt = (int)lroundf(humidity_target);
  if (barCur != ui.barCur || barTgt != ui.barTgt) {
    ui.barCur = barCur; ui.barTgt = barTgt;
    drawBand(HERO_X + 12, HERO_Y + 108, 204, 9, rh, humidity_target);
  }

  // ---- sensor 1 ----
  if (s1_ok) snprintf(buf, sizeof(buf), "%.1f C", s1_temp); else snprintf(buf, sizeof(buf), "--");
  if (diff(ui.s1t, buf))
    drawTextCell(SEN1_X + 10, SEN_Y + 15, 92, 21, buf, &FreeSansBold12pt7b,
                 s1_ok ? C_TX : C_BAD, C_CARD, 0);
  if (s1_ok) snprintf(buf, sizeof(buf), "%.0f%% RH", s1_rh); else snprintf(buf, sizeof(buf), "offline");
  if (diff(ui.s1rh, buf))
    drawTextCell(SEN1_X + 10, SEN_Y + 35, 92, 16, buf, &FreeSans9pt7b,
                 s1_ok ? C_ACCENT : C_BAD, C_CARD, 0);

  // ---- sensor 2 ----
  if (s2_ok) snprintf(buf, sizeof(buf), "%.1f C", s2_temp); else snprintf(buf, sizeof(buf), "--");
  if (diff(ui.s2t, buf))
    drawTextCell(SEN2_X + 10, SEN_Y + 15, 92, 21, buf, &FreeSansBold12pt7b,
                 s2_ok ? C_TX : C_BAD, C_CARD, 0);
  if (s2_ok) snprintf(buf, sizeof(buf), "%.0f%% RH", s2_rh); else snprintf(buf, sizeof(buf), "offline");
  if (diff(ui.s2rh, buf))
    drawTextCell(SEN2_X + 10, SEN_Y + 35, 92, 16, buf, &FreeSans9pt7b,
                 s2_ok ? C_ACCENT : C_BAD, C_CARD, 0);

  // ---- device rows ----
  for (int i = 0; i < NUM_DEVICES; i++) {
    int ry = DEV_Y + i * DEV_ROWH;
    int cy = ry + DEV_ROWH / 2;
    bool on = devices[i].state;

    int8_t on8 = on ? 1 : 0;
    if (ui.devOn[i] != on8) {
      ui.devOn[i] = on8;
      tft.fillCircle(DEV_X + 15, cy, 4, on ? kindColor(devices[i].kind) : C_OFF);
    }

    if (devices[i].kind == KIND_MISTING) {
      if (mr > 0)                              snprintf(buf, sizeof(buf), "ON %lus", mr);
      else if (devices[i].mode == MODE_OFF)    snprintf(buf, sizeof(buf), "OFF");
      else if (mist_period_min > 0) { unsigned long nx = mistNextSec(); snprintf(buf, sizeof(buf), "%lu:%02lu", nx / 60UL, nx % 60UL); }
      else                                     snprintf(buf, sizeof(buf), "off");
    } else {
      if      (devices[i].mode == MODE_ON)  snprintf(buf, sizeof(buf), "ON");
      else if (devices[i].mode == MODE_OFF) snprintf(buf, sizeof(buf), "OFF");
      else                                  snprintf(buf, sizeof(buf), on ? "ON" : "off");
    }
    if (diff(ui.devState[i], buf)) {
      uint16_t col = on ? kindColor(devices[i].kind) : C_DIM;
      drawTextCell(DEV_X + DEV_W - 92, ry + 1, 88, DEV_ROWH - 2, buf,
                   &FreeSans9pt7b, col, C_CARD, 2);
    }
  }
}

// ============================================================================
// WEB UI
// ============================================================================
const char HTML_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Climate</title><style>
:root{--bg:#0a0a0a;--card:#161616;--bd:#262626;--tx:#f5f5f5;--dim:#888;--ac:#3b82f6;--on:#10b981;--of:#3a3a3a;--bad:#e0626f;--r:18px}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
html,body{min-height:100%}
body{font:clamp(14px,1.4vw,16px) -apple-system,system-ui,sans-serif;background:var(--bg);color:var(--tx);padding:clamp(12px,2.5vw,24px);max-width:min(760px,100%);margin:0 auto;overscroll-behavior:contain}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:6px 6px clamp(14px,2vw,22px);font-size:clamp(11px,1.1vw,12px);color:var(--dim);letter-spacing:1.2px;text-transform:uppercase}
.dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--on);margin-right:8px;animation:pulse 2s infinite}
.hdr-stat{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
.chip{font-size:10px;letter-spacing:1px;padding:4px 8px;border-radius:8px;background:var(--of);color:var(--dim);font-weight:600;text-transform:uppercase}
.chip.on{background:rgba(16,185,129,.18);color:var(--on)}
.chip.purge{background:var(--card);color:var(--dim);text-transform:none;letter-spacing:.5px;border:1px solid var(--bd)}
.chip.purge.active{background:rgba(224,98,111,.18);color:var(--bad);animation:pulse 1.4s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}

/* SENSORS */
.sensors{display:grid;grid-template-columns:1fr 1fr;gap:clamp(10px,1.5vw,16px);margin-bottom:clamp(14px,2vw,20px)}
.sensor{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:clamp(18px,3vw,26px) clamp(16px,2.5vw,22px);position:relative;overflow:hidden}
.s-l{font-size:clamp(10px,1vw,11px);color:var(--dim);letter-spacing:1.6px;text-transform:uppercase;margin-bottom:clamp(10px,1.5vw,14px)}
.s-rh{font-size:clamp(44px,9vw,68px);font-weight:200;line-height:1;letter-spacing:-2px}
.s-rh .u{font-size:clamp(20px,4vw,28px);color:var(--dim);font-weight:300;margin-left:3px;letter-spacing:0}
.s-temp{font-size:clamp(13px,1.6vw,16px);color:var(--dim);margin-top:clamp(8px,1.2vw,10px);font-weight:400}
.sensor.bad .s-rh,.sensor.bad .s-temp{opacity:.35}
.sensor.bad::after{content:'OFFLINE';position:absolute;top:14px;right:14px;font-size:9px;letter-spacing:1.5px;color:var(--bad);font-weight:600}

/* Cards */
.card{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:clamp(18px,2.5vw,24px);margin-bottom:clamp(12px,1.8vw,16px)}
.card-t{font-size:clamp(10px,1.1vw,11px);color:var(--dim);letter-spacing:1.6px;text-transform:uppercase;margin-bottom:clamp(12px,2vw,16px);display:flex;justify-content:space-between;align-items:center;gap:10px}
.hint{font-size:clamp(9px,1vw,10px);color:#555;letter-spacing:.8px;text-transform:none;font-weight:400}

/* Dials */
.dial-wrap{position:relative;width:clamp(220px,55vw,280px);height:clamp(220px,55vw,280px);margin:0 auto;touch-action:none;user-select:none}
.dial-svg{width:100%;height:100%;cursor:grab;display:block}
.dial-svg:active{cursor:grabbing}
.dial-arc{fill:none;stroke-linecap:round;stroke-width:9}
.dial-bg{stroke:#222}
.dial-val-arc{stroke:var(--ac)}
.dial-cur-tick{fill:var(--dim)}
.dial-h{fill:#fff;stroke:var(--ac);stroke-width:3;filter:drop-shadow(0 2px 10px rgba(0,0,0,.6));transition:r .15s}
.dial-svg:active .dial-h{r:17}
.dial-c{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;pointer-events:none}
.dial-num{font-size:clamp(52px,11vw,72px);font-weight:200;line-height:1;letter-spacing:-3px}
.dial-unit{font-size:clamp(20px,4vw,26px);color:var(--dim);font-weight:300;margin-left:3px}
.dial-now{font-size:clamp(10px,1.1vw,11px);color:var(--dim);margin-top:clamp(8px,1.2vw,12px);letter-spacing:1.2px;text-transform:uppercase}

/* Devices */
.dev{display:flex;align-items:center;justify-content:space-between;padding:clamp(12px,1.8vw,16px) 2px;border-bottom:1px solid var(--bd);gap:10px;background:var(--card);position:relative}
.dev:last-child{border-bottom:none;padding-bottom:2px}
.dev:not(.dragging){transition:transform .22s cubic-bezier(.4,0,.2,1)}
.dev.dragging{opacity:.92;z-index:10;border-radius:10px;box-shadow:0 8px 24px rgba(0,0,0,.5);border:1px solid var(--bd);transition:none}
.grip{display:grid;grid-template-columns:1fr 1fr;gap:2px;padding:8px 4px 8px 0;cursor:grab;touch-action:none;flex-shrink:0;align-self:center}
.grip:active{cursor:grabbing}
.grip span{width:3px;height:3px;border-radius:50%;background:#3a3a3a;transition:background .15s}
.grip:hover span{background:var(--dim)}
.dev-l{display:flex;align-items:center;gap:12px;min-width:0;flex:1}
.dev-d{width:9px;height:9px;border-radius:50%;background:var(--of);transition:background .3s,box-shadow .3s;flex-shrink:0}
.dev-d.on{background:var(--on);box-shadow:0 0 10px rgba(16,185,129,.55)}
.dev-i{display:flex;flex-direction:column;min-width:0}
.dev-n{font-size:clamp(14px,1.5vw,16px);font-weight:500}
.dev-s{font-size:clamp(10px,1.05vw,11px);color:var(--dim);letter-spacing:1px;text-transform:uppercase;margin-top:3px}

/* Mode pill */
.pill{display:grid;grid-template-columns:repeat(3,1fr);background:#0c0c0c;border:1px solid var(--bd);border-radius:11px;padding:3px;position:relative;width:clamp(132px,17vw,156px);flex-shrink:0}
.pill button{background:transparent;border:0;color:var(--dim);font-size:clamp(10px,1.05vw,11px);font-weight:600;padding:7px 0;letter-spacing:1.2px;cursor:pointer;border-radius:8px;z-index:1;position:relative;transition:color .2s;font-family:inherit}
.pill button.act{color:#fff}
.pill .ind{position:absolute;top:3px;bottom:3px;width:calc(33.33% - 2px);left:3px;border-radius:8px;transition:transform .28s cubic-bezier(.4,0,.2,1),background .28s}
.pill[data-m="auto"] .ind{transform:translateX(0);background:var(--ac)}
.pill[data-m="on"] .ind{transform:translateX(calc(100% + 2px));background:var(--on)}
.pill[data-m="off"] .ind{transform:translateX(calc(200% + 4px));background:#666}

/* Advanced */
details summary{cursor:pointer;font-size:clamp(10px,1.1vw,11px);color:var(--dim);letter-spacing:1.5px;text-transform:uppercase;padding:clamp(12px,2vw,16px) clamp(16px,2.2vw,20px);background:var(--card);border:1px solid var(--bd);border-radius:var(--r);list-style:none;display:flex;justify-content:space-between;align-items:center;gap:10px}
details summary::-webkit-details-marker{display:none}
details[open] summary{border-radius:var(--r) var(--r) 0 0;border-bottom:none}
.saved{font-size:10px;color:var(--on);letter-spacing:1.5px;opacity:0;transition:opacity .25s}
.saved.show{opacity:1}
.adv{padding:8px clamp(16px,2.2vw,20px) clamp(14px,2vw,18px);background:var(--card);border:1px solid var(--bd);border-top:none;border-radius:0 0 var(--r) var(--r)}
.adv-grp{font-size:clamp(9px,1vw,10px);color:#666;letter-spacing:1.8px;text-transform:uppercase;margin:clamp(12px,1.8vw,16px) 0 4px;font-weight:600}
.adv-grp:first-child{margin-top:0}
.adv-r{display:flex;justify-content:space-between;align-items:center;padding:8px 0;gap:14px}
.adv-r label{font-size:clamp(12px,1.3vw,14px);color:#bbb}
.adv-r input{background:#0c0c0c;border:1px solid var(--bd);color:var(--tx);padding:8px 12px;border-radius:8px;width:88px;font-size:clamp(12px,1.3vw,14px);text-align:right;font-family:inherit}
.adv-r input:focus{outline:none;border-color:var(--ac)}
.note{font-size:clamp(10px,1.05vw,11px);color:var(--dim);margin-top:10px;line-height:1.55}

/* Wider screens — dials side by side */
@media (min-width:760px){
  .dials{display:grid;grid-template-columns:1fr 1fr;gap:16px}
  .dials .card{margin-bottom:0}
  .dials-wrap{margin-bottom:clamp(12px,1.8vw,16px)}
}
@media (max-width:340px){
  .sensors{grid-template-columns:1fr}
}
</style></head><body>
<div class="hdr">
  <span><span class="dot"></span>Connected</span>
  <div class="hdr-stat">
    <span class="chip" id="cHM">HUM</span>
    <span class="chip" id="cHT">MIST</span>
    <span class="chip" id="cHE">HEAT</span>
    <span class="chip" id="cMX">check</span>
    <span class="chip" id="cEX">EXH</span>
    <span class="chip purge" id="cPG">valve armed</span>
  </div>
  <span id="ip">192.168.4.1</span>
</div>

<div class="sensors">
  <div class="sensor" id="sen1"><div class="s-l">Sensor 1</div>
    <div class="s-rh"><span id="s1rh">--</span><span class="u">%</span></div>
    <div class="s-temp"><span id="s1t">--</span> &deg;C</div>
  </div>
  <div class="sensor" id="sen2"><div class="s-l">Sensor 2</div>
    <div class="s-rh"><span id="s2rh">--</span><span class="u">%</span></div>
    <div class="s-temp"><span id="s2t">--</span> &deg;C</div>
  </div>
</div>

<div class="dials-wrap"><div class="dials">
  <div class="card"><div class="card-t"><span>Humidity Target</span></div>
    <div class="dial-wrap" id="dh"><svg class="dial-svg" viewBox="-120 -120 240 240">
      <path class="dial-arc dial-bg" d="M -60.81 60.81 A 86 86 0 1 1 60.81 60.81"/>
      <path class="dial-arc dial-val-arc"/><circle class="dial-cur-tick" r="3" cx="0" cy="-102"/>
      <circle class="dial-h" r="15" cx="0" cy="-86"/></svg>
      <div class="dial-c"><div class="dial-num"><span>85</span><span class="dial-unit">%</span></div><div class="dial-now">now --%</div></div>
    </div>
  </div>
  <div class="card"><div class="card-t"><span>Misting Interval</span></div>
    <div class="dial-wrap" id="dt"><svg class="dial-svg" viewBox="-120 -120 240 240">
      <path class="dial-arc dial-bg" d="M -60.81 60.81 A 86 86 0 1 1 60.81 60.81"/>
      <path class="dial-arc dial-val-arc"/><circle class="dial-cur-tick" r="3" cx="0" cy="-102"/>
      <circle class="dial-h" r="15" cx="0" cy="-86"/></svg>
      <div class="dial-c"><div class="dial-num"><span>180</span><span class="dial-unit">m</span></div><div class="dial-now">every cycle</div></div>
    </div>
  </div>
</div></div>

<div class="card"><div class="card-t"><span>Devices</span><span class="hint">drag &#8942;&#8942; to reorder</span></div><div id="devs"></div></div>

<details><summary><span>Advanced</span><span class="saved" id="sv">SAVED</span></summary>
<div class="adv">
  <div class="adv-grp">Hysteresis</div>
  <div class="adv-r"><label>Humidity band (% RH)</label><input id="hb" type="number" step="0.5" min="1" max="20"></div>

  <div class="adv-grp">Temperature target</div>
  <div class="adv-r"><label>Target (&deg;C)</label><input id="temp" type="number" step="0.5" min="15" max="30"></div>

  <div class="adv-grp">Sensor balance</div>
  <div class="adv-r"><label>Max RH spread (% RH)</label><input id="rbb" type="number" step="0.5" min="0.5" max="10"></div>

  <div class="adv-grp">Exhaust valve</div>
  <div class="adv-r"><label>Purge cooldown (min)</label><input id="pcd" type="number" step="5" min="0" max="240"></div>

  <div class="adv-grp">Fresh Air (exhaust)</div>
  <div class="adv-r"><label>Run every (min)</label><input id="fp" type="number" step="5" min="0" max="240"></div>
  <div class="adv-r"><label>For (sec)</label><input id="fd" type="number" step="30" min="0" max="600"></div>

  <div class="adv-grp">Misting / surface wetting</div>
  <div class="adv-r"><label>Mist every (min)</label><input id="mp" type="number" step="15" min="0" max="720"></div>
  <div class="adv-r"><label>Mist for (sec)</label><input id="md" type="number" step="5" min="0" max="120"></div>

  <div class="note">Set period to 0 to disable a cycle. Sensor disagreement holds humidification. Heater output remains disabled in this bench build; its cutoff and cooldown require thermal commissioning.</div>
</div></details>

<script>
function dial(id, min, max, unit, onCommit){
  const w=document.getElementById(id), svg=w.querySelector('svg'), va=w.querySelector('.dial-val-arc'),
        h=w.querySelector('.dial-h'), vt=w.querySelector('.dial-num span'),
        cn=w.querySelector('.dial-now'), ct=w.querySelector('.dial-cur-tick');
  const R=86, RT=102;
  const polar=(a,r)=>{const rad=(a-90)*Math.PI/180;return[Math.cos(rad)*r,Math.sin(rad)*r]};
  const v2a=v=>-135+(v-min)/(max-min)*270;
  let val=(min+max)/2, drag=false, hold=false;
  function render(){
    const a=v2a(val), [hx,hy]=polar(a,R);
    h.setAttribute('cx',hx); h.setAttribute('cy',hy);
    if(a>-134.5){
      const [sx,sy]=polar(-135,R), [ex,ey]=polar(a,R);
      const lg=(a+135)>180?1:0;
      va.setAttribute('d',`M ${sx} ${sy} A ${R} ${R} 0 ${lg} 1 ${ex} ${ey}`);
    } else va.setAttribute('d','');
    vt.textContent=Math.round(val);
  }
  function p2v(e){
    const r=svg.getBoundingClientRect(), cx=r.left+r.width/2, cy=r.top+r.height/2;
    let d=Math.atan2(e.clientY-cy, e.clientX-cx)*180/Math.PI+90;
    if(d>180)d-=360; if(d<-135)d=-135; if(d>135)d=135;
    return Math.round(min+(d+135)/270*(max-min));
  }
  svg.addEventListener('pointerdown',e=>{e.preventDefault();drag=true;hold=true;svg.setPointerCapture(e.pointerId);const v=p2v(e);if(v!==val){val=v;render();if(navigator.vibrate)navigator.vibrate(3)}});
  svg.addEventListener('pointermove',e=>{if(!drag)return;const v=p2v(e);if(v!==val){val=v;render();if(navigator.vibrate)navigator.vibrate(3)}});
  const end=e=>{if(!drag)return;drag=false;try{svg.releasePointerCapture(e.pointerId)}catch(_){}onCommit(val);setTimeout(()=>hold=false,1500)};
  svg.addEventListener('pointerup',end); svg.addEventListener('pointercancel',end);
  return {
    set(v){if(hold)return;val=v;render()},
    cur(c){const cl=Math.max(min,Math.min(max,c));const[x,y]=polar(v2a(cl),RT);ct.setAttribute('cx',x);ct.setAttribute('cy',y);cn.textContent='now '+Math.round(c)+unit}
  };
}

const STATE = { dragging: false, drag: null };

function fmtTime(sec){
  sec = Math.max(0, Math.round(sec || 0));
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  return m > 0 ? `${m}:${String(s).padStart(2,'0')}` : `${s}s`;
}

function devHTML(devs, order, st){
  return order.map(i => {
    const d = devs[i];
    const isMist = d.kind === 'misting';
    const isHeater = d.kind === 'heater';

    const mk = isMist && st.mist_manual
      ? 'on'
      : d.mode === 'force_on'
        ? 'on'
        : d.mode === 'force_off'
          ? 'off'
          : 'auto';

    let sub;

    if (isHeater) {
      sub = st.heater.commissioned ? (d.on ? 'temperature control running' : 'temperature control idle') : 'bench lock: heater disabled';
    } else if (isMist) {
      if (st.mist_manual) {
        sub = `manual mist &middot; ${fmtTime(st.mist_remaining)} left`;
      } else if (st.mist_running) {
        sub = `auto mist &middot; ${fmtTime(st.mist_remaining)} left`;
      } else if (d.mode === 'force_off') {
        sub = 'scheduled misting disabled';
      } else if (st.mist_next > 0) {
        sub = `auto &middot; next in ${fmtTime(st.mist_next)}`;
      } else {
        sub = 'auto &middot; ready';
      }
    } else {
      sub = d.mode === 'auto'
        ? (d.on ? 'auto &middot; running' : 'auto &middot; idle')
        : (d.mode === 'force_on' ? 'forced on' : 'forced off');
    }

    const midLabel = isMist ? 'MIST' : 'ON';

    return `<div class="dev" data-id="${i}">
      <div class="grip" aria-label="Drag to reorder"><span></span><span></span><span></span><span></span><span></span><span></span></div>
      <div class="dev-l">
        <div class="dev-d${d.on ? ' on' : ''}"></div>
        <div class="dev-i"><div class="dev-n">${d.name}</div><div class="dev-s">${sub}</div></div>
      </div>
      <div class="pill" data-m="${mk}" data-i="${i}">
        <button data-k="auto"${mk === 'auto' ? ' class="act"' : ''}>AUTO</button>
        <button data-k="on"${isHeater ? ' disabled title="Manual heater ON is prohibited"' : (mk === 'on' ? ' class="act"' : '')}>${isHeater ? 'NO MANUAL' : midLabel}</button>
        <button data-k="off"${mk === 'off' ? ' class="act"' : ''}>OFF</button>
        <div class="ind"></div>
      </div>
    </div>`;
  }).join('');
}

function bindDevs(){
  // Mode pill buttons
  document.querySelectorAll('.pill').forEach(p => {
    p.querySelectorAll('button').forEach(b => {
      b.onclick = () => {
        const i = p.dataset.i, k = b.dataset.k;
        p.dataset.m = k;
        p.querySelectorAll('button').forEach(x => x.classList.toggle('act', x === b));
        if (navigator.vibrate) navigator.vibrate(8);
        fetch('/api/device/'+i+'/'+k, {method:'POST'}).then(refresh);
      };
    });
  });

  // Reorder drag handles
  const cont = document.getElementById('devs');
  cont.querySelectorAll('.grip').forEach(grip => {
    grip.addEventListener('pointerdown', e => {
      e.preventDefault();
      const row = grip.closest('.dev');
      const rows = [...cont.querySelectorAll('.dev')];
      const rects = rows.map(r => r.getBoundingClientRect());
      const fromIdx = rows.indexOf(row);
      STATE.dragging = true;
      STATE.drag = { row, rows, rects, fromIdx, toIdx: fromIdx, startY: e.clientY, pid: e.pointerId, grip };
      row.classList.add('dragging');
      try { grip.setPointerCapture(e.pointerId); } catch(_) {}
      if (navigator.vibrate) navigator.vibrate(10);
    });
    grip.addEventListener('pointermove', e => {
      const d = STATE.drag; if (!d) return;
      const dy = e.clientY - d.startY;
      d.row.style.transform = `translateY(${dy}px)`;
      const rowH = d.rects[d.fromIdx].height;
      const draggedCenter = d.rects[d.fromIdx].top + d.rects[d.fromIdx].height/2 + dy;
      let newIdx = d.fromIdx;
      for (let i = 0; i < d.rows.length; i++) {
        if (i === d.fromIdx) continue;
        const cm = d.rects[i].top + d.rects[i].height/2;
        if (i < d.fromIdx && draggedCenter < cm)      newIdx = Math.min(newIdx, i);
        else if (i > d.fromIdx && draggedCenter > cm) newIdx = Math.max(newIdx, i);
      }
      if (newIdx !== d.toIdx) {
        d.toIdx = newIdx;
        d.rows.forEach((r, i) => {
          if (r === d.row) return;
          let offset = 0;
          if (d.fromIdx < newIdx && i > d.fromIdx && i <= newIdx) offset = -rowH;
          else if (d.fromIdx > newIdx && i >= newIdx && i < d.fromIdx) offset = rowH;
          r.style.transform = `translateY(${offset}px)`;
        });
      }
    });
    const finish = (e, commit) => {
      const d = STATE.drag; if (!d) return;
      d.rows.forEach(r => r.style.transform = '');
      d.row.classList.remove('dragging');
      try { d.grip.releasePointerCapture(d.pid); } catch(_) {}
      if (commit && d.toIdx !== d.fromIdx) {
        const ids = d.rows.map(r => +r.dataset.id);
        const [moved] = ids.splice(d.fromIdx, 1);
        ids.splice(d.toIdx, 0, moved);
        fetch('/api/order?o=' + ids.join(','), {method:'POST'}).then(() => {
          STATE.dragging = false; STATE.drag = null; refresh();
        });
      } else {
        STATE.dragging = false; STATE.drag = null;
      }
    };
    grip.addEventListener('pointerup',     e => finish(e, true));
    grip.addEventListener('pointercancel', e => finish(e, false));
  });
}

const dH = dial('dh',30,95,'%', v => fetch('/api/target?hum='+v,{method:'POST'}).then(refresh));
const dM = dial('dt',30,720,'m', v => fetch('/api/config?mp='+v,{method:'POST'}).then(refresh));

let savedTimer;
function flashSaved(){
  const el = document.getElementById('sv');
  el.classList.add('show');
  clearTimeout(savedTimer);
  savedTimer = setTimeout(() => el.classList.remove('show'), 1200);
}

function debounce(fn, ms){
  let t;
  return (...args) => { clearTimeout(t); t = setTimeout(() => fn(...args), ms); };
}
document.getElementById('temp').addEventListener('change', e => {
  const v = parseFloat(e.target.value);
  if (Number.isFinite(v)) fetch('/api/target?temp='+v,{method:'POST'}).then(refresh);
});
const saveCfg = debounce((field, val) => {
  fetch('/api/config?'+field+'='+val, {method:'POST'}).then(() => { flashSaved(); refresh(); });
}, 500);

['hb','fp','fd','mp','md','rbb','pcd'].forEach(id => {
  document.getElementById(id).addEventListener('input', e => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) saveCfg(id, v);
  });
});

async function refresh(){
  if (STATE.dragging) return;
  try {
    const d = await (await fetch('/api/status')).json();
    document.getElementById('s1rh').textContent = d.s1.ok ? d.s1.rh.toFixed(0)   : '--';
    document.getElementById('s1t' ).textContent = d.s1.ok ? d.s1.temp.toFixed(1) : '--';
    document.getElementById('s2rh').textContent = d.s2.ok ? d.s2.rh.toFixed(0)   : '--';
    document.getElementById('s2t' ).textContent = d.s2.ok ? d.s2.temp.toFixed(1) : '--';
    document.getElementById('sen1').classList.toggle('bad', !d.s1.ok);
    document.getElementById('sen2').classList.toggle('bad', !d.s2.ok);
    dH.set(d.hum_target);  if (d.ctrl_rh >= 0) dH.cur(d.ctrl_rh);
    dM.set(d.mist_period);

    document.getElementById('devs').innerHTML = devHTML(d.devices, d.order, d);

    document.getElementById('ip').textContent = d.ip;

    const byKind = {};
    d.devices.forEach(dev => byKind[dev.kind] = dev.on);
    const chip = (id, on) => document.getElementById(id).classList.toggle('on', !!on);
    chip('cHM', byKind.humidifier); chip('cHT', byKind.misting);
    chip('cHE', byKind.heater);     chip('cEX', byKind.exhaust);
    chip('cMX', d.rh_balance_hold);

    const pg = document.getElementById('cPG');
    if (d.purging) {
      pg.classList.add('active'); pg.textContent = 'purging';
    } else if (d.cooldown_remaining > 0) {
      pg.classList.remove('active');
      const m = Math.floor(d.cooldown_remaining / 60), s = d.cooldown_remaining % 60;
      pg.textContent = 'next purge ' + m + ':' + String(s).padStart(2,'0');
    } else {
      pg.classList.remove('active'); pg.textContent = 'valve armed';
    }

    const setIfBlur = (id, v) => {
      const el = document.getElementById(id);
      if (document.activeElement !== el) el.value = v;
    };
    setIfBlur('temp', d.temp_target);
    setIfBlur('hb', d.hum_band);
    setIfBlur('fp', d.fae_period);
    setIfBlur('fd', d.fae_duration);
    setIfBlur('mp', d.mist_period);
    setIfBlur('md', d.mist_duration);
    setIfBlur('rbb', d.rh_balance_band);
    setIfBlur('pcd', d.purge_cooldown);

    bindDevs();
  } catch(e) {}
}

refresh();
setInterval(refresh, 2000);
</script></body></html>)HTML";

const char* modeStr(DeviceMode m) {
  return m == MODE_AUTO ? "auto" : m == MODE_ON ? "force_on" : "force_off";
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", HTML_PAGE);
}

static inline float jf(float v) { return isfinite(v) ? v : 0.0f; }   // JSON has no NaN

String currentIp() {
  if (wifiState == WS_CONNECTED) return WiFi.localIP().toString();
  if (apActive) return WiFi.softAPIP().toString();
  return "";
}

const char* wifiStateStr() {
  switch (wifiState) {
    case WS_NO_CREDS:   return "no_credentials";
    case WS_CONNECTING: return "connecting";
    case WS_CONNECTED:  return "connected";
    case WS_WAIT_RETRY: return "retry_wait";
  }
  return "?";
}

void handleStatus() {
  unsigned long now = millis();
  unsigned long cooldownRemain = 0;
  unsigned long mistRemain = mistRemainingSec();
  unsigned long mistNext   = mistNextSec();

  if (purge_cooldown_min > 0 && !exhaustPurging) {
    unsigned long elapsed = now - lastPurgeEndAt;
    unsigned long cd      = (unsigned long)(purge_cooldown_min * 60000.0f);
    if (elapsed < cd) cooldownRemain = (cd - elapsed) / 1000UL;
  }

  int iHum = findIdx(KIND_HUMIDIFIER);
  unsigned long humManualRemain = 0;
  if (iHum >= 0 && devices[iHum].mode == MODE_ON) {
    unsigned long e = now - humManualOnAt;
    if (e < MANUAL_HUMIDIFIER_MAX_RUNTIME_MS) humManualRemain = (MANUAL_HUMIDIFIER_MAX_RUNTIME_MS - e) / 1000UL;
  }

  time_t epoch = time(nullptr);
  bool timeValid = epoch > 1700000000L;

  char otaRes[sizeof(otaResult)], otaAvail[sizeof(otaAvailable)];
  portENTER_CRITICAL(&otaMux);
  strncpy(otaRes, otaResult, sizeof(otaRes)); otaRes[sizeof(otaRes) - 1] = 0;
  strncpy(otaAvail, otaAvailable, sizeof(otaAvail)); otaAvail[sizeof(otaAvail) - 1] = 0;
  portEXIT_CRITICAL(&otaMux);

  static char buf[4200]; int n = 0;   // static: keep it off the loop task stack
  float ctrl_rh   = controlRH_low();
  float ctrl_temp = controlTemp_low();

  // ---- identity / network / time / ota (new) ----
  n += snprintf(buf+n, sizeof(buf)-n,
    "{\"fw\":\"%s\",\"build\":\"%s\",\"family\":\"%s\",\"device_id\":\"%s\",\"device_name\":\"%s\","
    "\"uptime_s\":%lu,\"free_heap\":%lu,\"reset_reason\":%d,"
    "\"ip\":\"%s\",\"wifi_state\":\"%s\",\"wifi_ssid\":\"%s\",\"rssi\":%d,"
    "\"ap_active\":%s,\"ap_ssid\":\"%s\","
    "\"time_valid\":%s,\"epoch\":%ld,"
    "\"ota\":{\"enabled\":false,\"busy\":%s,\"configured\":%s,\"interval_min\":%lu,\"checks\":%lu,"
    "\"last_check_age_s\":%ld,\"result\":\"%s\",\"available\":\"%s\"},",
    FW_VERSION, FW_BUILD_ID, FW_DEVICE_FAMILY, deviceId, deviceName.c_str(),
    now / 1000UL, (unsigned long)ESP.getFreeHeap(), (int)esp_reset_reason(),
    currentIp().c_str(), wifiStateStr(), wifiSsid.c_str(),
    (wifiState == WS_CONNECTED) ? WiFi.RSSI() : 0,
    apActive ? "true" : "false", apActive ? apSsid().c_str() : "",
    timeValid ? "true" : "false", timeValid ? (long)epoch : 0L,
    otaBusy ? "true" : "false", otaManifestUrl.length() ? "true" : "false",
    (unsigned long)otaIntervalMin, (unsigned long)otaChecks,
    otaLastCheckMs ? (long)((now - otaLastCheckMs) / 1000UL) : -1L,
    otaRes, otaAvail);

  // ---- existing fields, unchanged keys ----
  n += snprintf(buf+n, sizeof(buf)-n,
    "\"hum_target\":%.1f,\"hum_band\":%.1f,\"temp_target\":%.1f,\"temp_band\":%.1f,"
    "\"ctrl_rh\":%.2f,\"ctrl_temp\":%.2f,"
    "\"fae_period\":%.0f,\"fae_duration\":%.0f,"
    "\"mist_period\":%.0f,\"mist_duration\":%.0f,\"mist_running\":%s,\"mist_manual\":%s,"
    "\"mist_remaining\":%lu,\"mist_next\":%lu,"
    "\"rh_balance_band\":%.1f,\"rh_spread\":%.2f,\"rh_balance_hold\":%s,"
    "\"purge_cooldown\":%.0f,\"purging\":%s,\"cooldown_remaining\":%lu,"
    "\"fae_active\":%s,\"sensors_disagree\":%s,"
    "\"heater\":{\"commissioned\":%s,\"command_on\":%s,\"tripped\":%s,\"early_off_c\":%.1f,\"max_on_s\":%lu,\"min_off_s\":%lu},"
    "\"hum_manual_remaining\":%lu,\"hum_manual_timed_out\":%s,"
    "\"sensors_failed\":%s,"
    "\"s1\":{\"ok\":%s,\"temp\":%.2f,\"rh\":%.2f,\"healthy\":%s,\"fail_streak\":%u,\"fail_count\":%lu,\"last_good_age_s\":%ld},"
    "\"s2\":{\"ok\":%s,\"temp\":%.2f,\"rh\":%.2f,\"healthy\":%s,\"fail_streak\":%u,\"fail_count\":%lu,\"last_good_age_s\":%ld},"
    "\"devices\":[",
    humidity_target, humidity_band, temperature_target, temperature_band,
    jf(ctrl_rh), jf(ctrl_temp),
    fae_period_min, fae_duration_sec,
    mist_period_min, mist_duration_sec,
    (mistCycleRunning || mistManualRunning) ? "true" : "false",
    mistManualRunning ? "true" : "false",
    mistRemain, mistNext,
    rh_balance_band, jf(rhSensorSpread), rhBalanceHold ? "true" : "false",
    purge_cooldown_min, exhaustPurging ? "true" : "false", cooldownRemain,
    faeRunning ? "true" : "false", sensorsDisagree ? "true" : "false",
    HEATER_COMMISSIONED ? "true" : "false", heaterState.on ? "true" : "false", heaterState.tripped ? "true" : "false",
    heaterConfig.earlyOffC, (unsigned long)(heaterConfig.maxOnMs/1000UL), (unsigned long)(heaterConfig.minOffMs/1000UL),
    humManualRemain, humManualTimedOut ? "true" : "false",
    (!s1_ok && !s2_ok) ? "true" : "false",
    s1_ok ? "true" : "false", jf(s1_temp), jf(s1_rh), sh1.healthy ? "true" : "false", sh1.failStreak,
      (unsigned long)sh1.failCount, sh1.lastGoodMs ? (long)((now - sh1.lastGoodMs) / 1000UL) : -1L,
    s2_ok ? "true" : "false", jf(s2_temp), jf(s2_rh), sh2.healthy ? "true" : "false", sh2.failStreak,
      (unsigned long)sh2.failCount, sh2.lastGoodMs ? (long)((now - sh2.lastGoodMs) / 1000UL) : -1L);
  for (int i = 0; i < NUM_DEVICES; i++) {
    n += snprintf(buf+n, sizeof(buf)-n,
      "%s{\"name\":\"%s\",\"kind\":\"%s\",\"mode\":\"%s\",\"on\":%s}",
      i==0?"":",", devices[i].name, devices[i].kindStr,
      modeStr(devices[i].mode), devices[i].state?"true":"false");
  }
  n += snprintf(buf+n, sizeof(buf)-n, "],\"order\":[");
  for (int i = 0; i < NUM_DEVICES; i++) {
    n += snprintf(buf+n, sizeof(buf)-n, "%s%d", i==0?"":",", deviceOrder[i]);
  }
  n += snprintf(buf+n, sizeof(buf)-n, "]}");
  server.send(200, "application/json", buf);
}

void handleTarget() {
  if (server.hasArg("hum")) {
    humidity_target = clampf(server.arg("hum").toFloat(), 30, 95);
  }
  if (server.hasArg("temp")) {
    temperature_target = clampf(server.arg("temp").toFloat(), 15, 30);
  }
  updateRelays();
  saveSettings();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleConfig() {
  bool changed = false;
  if (server.hasArg("hb"))  { humidity_band       = clampf(server.arg("hb").toFloat(), 1,   20);  changed = true; }
  if (server.hasArg("tb"))  { temperature_band    = clampf(server.arg("tb").toFloat(), 0.5, 10);  changed = true; }
  if (server.hasArg("fp"))  { fae_period_min      = clampf(server.arg("fp").toFloat(), 0,   240); changed = true; }
  if (server.hasArg("fd"))  { fae_duration_sec    = clampf(server.arg("fd").toFloat(), 0,   600); changed = true; }
  if (server.hasArg("mp"))  { mist_period_min     = clampf(server.arg("mp").toFloat(), 0,   720); changed = true; }
  if (server.hasArg("md"))  { mist_duration_sec   = clampf(server.arg("md").toFloat(), 0,   120); changed = true; }
  if (server.hasArg("rbb")) { rh_balance_band     = clampf(server.arg("rbb").toFloat(), 0.5, 10); changed = true; }
  if (server.hasArg("pcd")) { purge_cooldown_min  = clampf(server.arg("pcd").toFloat(), 0,   240); changed = true; }
  if (changed) { updateRelays(); saveSettings(); }

  // System settings (separate NVS namespace). Not part of the climate config.
  bool sysChanged = false;
  if (server.hasArg("name") && server.arg("name").length() && server.arg("name").length() < 32) {
    deviceName = server.arg("name"); sysChanged = true;
  }
  if (false && server.hasArg("ota_url")) {
    String u = server.arg("ota_url");
    if (u.length() == 0 || u.startsWith("https://")) { otaManifestUrl = u; sysChanged = true; }
  }
  if (server.hasArg("ota_min")) {
    long m = server.arg("ota_min").toInt();
    if (m >= 1 && m <= 10080) { otaIntervalMin = (uint32_t)m; sysChanged = true; }
  }
  if (sysChanged) saveSystem();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleDevice() {
  int i = server.pathArg(0).toInt();
  String m = server.pathArg(1);
  if (i < 0 || i >= NUM_DEVICES) {
    server.send(400, "application/json", "{\"error\":\"bad device\"}");
    return;
  }

  // Safety: for misting, the MIST button starts a timed pulse instead of latching water on forever.
  // OFF disables scheduled misting until returned to AUTO.
  if (devices[i].kind == KIND_HEATER && m == "on") {
    server.send(403, "application/json", "{\"error\":\"manual heater ON is prohibited\"}");
    return;
  }
  if (devices[i].kind == KIND_MISTING && m == "on") {
    devices[i].mode = MODE_AUTO;
    startMistPulse();
  } else {
    devices[i].mode = (m == "on") ? MODE_ON : (m == "off") ? MODE_OFF : MODE_AUTO;
    if (devices[i].kind == KIND_HEATER && devices[i].mode == MODE_ON) devices[i].mode = MODE_AUTO;
    if (devices[i].kind == KIND_HUMIDIFIER) {
      humManualTimedOut = false;
      if (devices[i].mode == MODE_ON) humManualOnAt = millis();   // start the bounded-runtime clock
    }
  }

  updateRelays();
  saveSettings();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleOrder() {
  if (!server.hasArg("o")) {
    server.send(400, "application/json", "{\"error\":\"missing o\"}");
    return;
  }
  String s = server.arg("o");
  uint8_t newOrder[NUM_DEVICES];
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)s.length() && idx < NUM_DEVICES; i++) {
    if (i == (int)s.length() || s[i] == ',') {
      int v = s.substring(start, i).toInt();
      if (v < 0 || v >= NUM_DEVICES) {
        server.send(400, "application/json", "{\"error\":\"bad value\"}");
        return;
      }
      newOrder[idx++] = (uint8_t)v;
      start = i + 1;
    }
  }
  if (idx != NUM_DEVICES) {
    server.send(400, "application/json", "{\"error\":\"bad count\"}");
    return;
  }
  bool seen[NUM_DEVICES] = {false};
  for (int i = 0; i < NUM_DEVICES; i++) {
    if (seen[newOrder[i]]) {
      server.send(400, "application/json", "{\"error\":\"duplicate\"}");
      return;
    }
    seen[newOrder[i]] = true;
  }
  memcpy(deviceOrder, newOrder, NUM_DEVICES);
  saveSettings();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ============================================================================
// WI-FI: STA normally, temporary provisioning AP when needed. Fully
// non-blocking. Nothing here is ever waited on by the control loop.
// ============================================================================
String apSsid() { return String("SHUNYA-") + String(deviceId + 8); }        // last 4 hex of MAC
String apPass() { return String("shunya") + String(deviceId + 8); }        // 10 chars, shown on Serial

void startAP() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);                    // STA interface stays alive for scans and retries
  WiFi.softAP(apSsid().c_str(), apPass().c_str());
  apActive = true;
  apStartedAt = millis();
  Serial.printf("[wifi] provisioning AP up: ssid=%s pass=%s ip=%s\n",
                apSsid().c_str(), apPass().c_str(), WiFi.softAPIP().toString().c_str());
}

void stopAP() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("[wifi] provisioning AP stopped");
}

void staBegin() {
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());   // returns immediately
  wifiState = WS_CONNECTING;
  wifiStateAt = millis();
  Serial.printf("[wifi] STA connecting to %s\n", wifiSsid.c_str());
}

void onStaUp() {
  wifiState = WS_CONNECTED;
  staConnectedAt = millis();
  wifiRetryMs = STA_RETRY_MIN_MS;
  staDownSince = 0;
  Serial.printf("[wifi] STA up ip=%s ch=%d rssi=%d\n",
                WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
  if (!ntpStarted) {
    configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);   // async; control never waits on it
    ntpStarted = true;
  }
  otaNextCheckAt = millis() + OTA_FIRST_CHECK_DELAY_MS;
  if (otaNextCheckAt == 0) otaNextCheckAt = 1;
}

void wifiSetup() {
  WiFi.persistent(false);          // credentials live in our NVS namespace, not the core's
  WiFi.setAutoReconnect(false);    // reconnection is handled by the state machine below
  WiFi.setSleep(false);
  WiFi.setHostname(deviceName.c_str());
  if (wifiSsid.length() == 0) {
    wifiState = WS_NO_CREDS;
    startAP();
    Serial.println("[wifi] no stored credentials, provisioning mode");
  } else {
    WiFi.mode(WIFI_STA);
    staDownSince = millis();
    staBegin();
  }
}

void serviceWifi() {
  unsigned long now = millis();
  wl_status_t st = WiFi.status();
  bool up = (st == WL_CONNECTED);

  switch (wifiState) {
    case WS_NO_CREDS:
      if (wifiSsid.length()) { staDownSince = now; staBegin(); }
      break;

    case WS_CONNECTING:
      if (up) onStaUp();
      else if (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED ||
               now - wifiStateAt >= STA_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect();
        wifiState = WS_WAIT_RETRY;
        wifiStateAt = now;
        Serial.printf("[wifi] STA attempt failed (status %d), retry in %lu ms\n", (int)st, wifiRetryMs);
      }
      break;

    case WS_WAIT_RETRY:
      if (now - wifiStateAt >= wifiRetryMs) {
        wifiRetryMs = (wifiRetryMs * 2 > STA_RETRY_MAX_MS) ? STA_RETRY_MAX_MS : wifiRetryMs * 2;   // 5, 10, 20, 40, 60 s
        staBegin();
      }
      break;

    case WS_CONNECTED:
      if (!up) {
        wifiState = WS_WAIT_RETRY;
        wifiStateAt = now;
        wifiRetryMs = STA_RETRY_MIN_MS;
        staDownSince = now;
        Serial.println("[wifi] STA lost, will retry");
      }
      break;
  }

  // Sustained STA failure with credentials present: raise the AP alongside so
  // someone on site can fix credentials. STA keeps retrying underneath.
  if (wifiState != WS_CONNECTED && wifiState != WS_NO_CREDS && !apActive &&
      staDownSince && now - staDownSince >= PROVISION_AFTER_STA_FAIL_MS) {
    Serial.println("[wifi] STA down for a long time, raising provisioning AP as well");
    startAP();
  }
  if (apRequested) { apRequested = false; if (!apActive) { startAP(); apStartedAt = now; } }

  // Once STA is up and has been for a while, drop the AP. If there are no
  // credentials at all the AP stays, because it is the only way in.
  if (apActive && wifiState == WS_CONNECTED && wifiSsid.length() &&
      now - staConnectedAt >= AP_LINGER_AFTER_STA_MS && now - apStartedAt >= AP_LINGER_AFTER_STA_MS) {
    stopAP();
  }
}

// ============================================================================
// PROVISIONING PAGE + WI-FI API
// ============================================================================
const char PROVISION_PAGE[] PROGMEM = R"PROV(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Wi-Fi setup</title>
<style>body{font-family:system-ui,sans-serif;background:#000;color:#eee;margin:16px;max-width:480px}
input,button{width:100%;padding:10px;margin:6px 0;box-sizing:border-box;font-size:1rem;border-radius:6px;border:1px solid #333;background:#161616;color:#eee}
button{background:#3b82f6;border:0;color:#fff}.n{padding:10px;border:1px solid #333;border-radius:6px;margin:4px 0;cursor:pointer}
small{color:#888}#info{color:#888;font-size:.85rem;margin-bottom:10px}</style></head><body>
<h2>Wi-Fi setup</h2><div id="info">loading...</div>
<button onclick="scan()">Scan networks</button><div id="nets"></div>
<input id="ssid" placeholder="SSID" autocapitalize="off" autocorrect="off">
<input id="pass" type="password" placeholder="Password">
<button onclick="save()">Save and connect</button><p id="msg"></p>
<p><small>The provisioning AP switches off a few minutes after the chamber joins your network.</small></p>
<script>
function scan(){document.getElementById('nets').textContent='scanning...';fetch('/api/wifi/scan?start=1').then(()=>setTimeout(poll,2000));}
function poll(){fetch('/api/wifi/scan').then(r=>r.json()).then(d=>{if(d.status==='running'){setTimeout(poll,1000);return;}
let h='';(d.nets||[]).forEach(n=>{h+='<div class="n" onclick="pick(this)" data-s="'+n.ssid.replace(/"/g,'&quot;')+'">'+n.ssid+' <small>'+n.rssi+' dBm ch'+n.ch+'</small></div>';});
document.getElementById('nets').innerHTML=h||'none found';});}
function pick(el){document.getElementById('ssid').value=el.getAttribute('data-s');document.getElementById('pass').focus();}
function save(){const b=new URLSearchParams();b.append('ssid',document.getElementById('ssid').value);b.append('pass',document.getElementById('pass').value);
fetch('/api/wifi',{method:'POST',body:b}).then(r=>r.json()).then(d=>{document.getElementById('msg').textContent=d.msg||JSON.stringify(d);});}
function st(){fetch('/api/status').then(r=>r.json()).then(d=>{document.getElementById('info').textContent=d.device_name+' ('+d.device_id+')  fw '+d.fw+'  wifi: '+d.wifi_state+(d.ip?'  ip '+d.ip:'');}).catch(()=>{});}
st();setInterval(st,4000);
</script></body></html>)PROV";

void handleProvisionPage() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", PROVISION_PAGE);
}

void handleWifiScan() {
  int n = WiFi.scanComplete();
  if (server.hasArg("start")) {
    if (n != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); WiFi.scanNetworks(true, true); }
    server.send(200, "application/json", "{\"status\":\"started\"}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) { server.send(200, "application/json", "{\"status\":\"running\"}"); return; }
  if (n < 0) { server.send(200, "application/json", "{\"status\":\"idle\",\"nets\":[]}"); return; }
  String j = "{\"status\":\"done\",\"nets\":[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i) j += ",";
    String ss = WiFi.SSID(i); ss.replace("\"", ""); ss.replace("\\", "");
    j += "{\"ssid\":\"" + ss + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"ch\":" + String(WiFi.channel(i)) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleWifiSave() {
  String s = server.hasArg("ssid") ? server.arg("ssid") : "";
  String p = server.hasArg("pass") ? server.arg("pass") : "";
  s.trim();
  if (s.length() == 0 || s.length() > 32 || p.length() > 63) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"bad ssid or password length\"}");
    return;
  }
  wifiSsid = s; wifiPass = p;
  saveSystem();
  Serial.printf("[wifi] credentials saved for %s\n", wifiSsid.c_str());
  WiFi.disconnect();
  wifiRetryMs = STA_RETRY_MIN_MS;
  staDownSince = millis();
  staBegin();
  server.send(200, "application/json",
    "{\"ok\":true,\"msg\":\"Saved. Connecting to " + s + ". Watch the TFT header for the new IP.\"}");
}

void handleWifiForget() {
  wifiSsid = ""; wifiPass = "";
  saveSystem();
  WiFi.disconnect();
  wifiState = WS_NO_CREDS;
  startAP();
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"credentials cleared, provisioning AP active\"}");
}

void handleProvisionRequest() {
  apRequested = true;
  server.send(200, "application/json",
    "{\"ok\":true,\"ap_ssid\":\"" + apSsid() + "\",\"msg\":\"provisioning AP starting\"}");
}

// ============================================================================
// OTA: pull-based HTTPS, verified, in its own task
//
//   manifest (HTTPS, TLS-validated)  -> {version,url,sha256,device_family}
//   validate manifest                -> family match, https url, 64-hex sha
//   compare version                  -> only strictly newer is installed
//   download to inactive slot        -> Update.write while hashing
//   verify SHA-256                   -> mismatch => Update.abort(), running image untouched
//   Update.end(true)                 -> otadata switches boot slot only here
//   reboot from the control loop     -> relays driven OFF first
//
// The running application is never modified. otadata is only rewritten by
// Update.end() after a complete, verified write. An interrupted download, a
// TLS failure, a bad hash or a power cut mid-transfer all leave the current
// firmware bootable.
//
// NOT PROVIDED BY THIS BUILD: first-boot health check with automatic rollback.
// The stock Arduino-ESP32 bootloader is built without
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, so a new image that boots and then
// crashes will keep being booted. That is a deliberate later step.
// ============================================================================
static void otaSetResult(const char* fmt, ...) {
  char tmp[sizeof(otaResult)];
  va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
  portENTER_CRITICAL(&otaMux);
  strncpy(otaResult, tmp, sizeof(otaResult)); otaResult[sizeof(otaResult) - 1] = 0;
  portEXIT_CRITICAL(&otaMux);
  Serial.printf("[ota] %s\n", tmp);
}

// Flat-JSON string/number extractor. Enough for the manifest, nothing more.
static bool jsonFind(const String& src, const char* key, String& out) {
  String pat = String("\"") + key + "\"";
  int k = src.indexOf(pat);
  if (k < 0) return false;
  int c = src.indexOf(':', k + pat.length());
  if (c < 0) return false;
  int i = c + 1;
  while (i < (int)src.length() && isspace((unsigned char)src[i])) i++;
  if (i >= (int)src.length()) return false;
  if (src[i] == '"') {
    int e = src.indexOf('"', i + 1);
    if (e < 0) return false;
    out = src.substring(i + 1, e);
  } else {
    int e = i;
    while (e < (int)src.length() && src[e] != ',' && src[e] != '}' && src[e] != '\n') e++;
    out = src.substring(i, e); out.trim();
  }
  return true;
}

// "0.1.1-bootstrap-test" -> {0,1,1}. Suffix after the numbers is ignored.
static bool parseVersion(const char* s, int v[3]) {
  v[0] = v[1] = v[2] = 0;
  int i = 0, part = 0;
  if (s[0] == 'v' || s[0] == 'V') i++;
  while (part < 3) {
    if (!isdigit((unsigned char)s[i])) return part > 0;   // need at least the major
    while (isdigit((unsigned char)s[i])) v[part] = v[part] * 10 + (s[i++] - '0');
    part++;
    if (s[i] == '.') { i++; continue; }
    break;
  }
  return true;
}

static int cmpVersion(const char* a, const char* b) {   // >0 if a newer than b
  int va[3], vb[3];
  if (!parseVersion(a, va) || !parseVersion(b, vb)) return 0;
  for (int i = 0; i < 3; i++) { if (va[i] != vb[i]) return va[i] > vb[i] ? 1 : -1; }
  return 0;
}

static bool isHex64(const String& s) {
  if (s.length() != 64) return false;
  for (size_t i = 0; i < 64; i++) if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}

// mbedtls 2.x (IDF 4.4, Arduino core 2.x) uses *_ret; mbedtls 3.x (IDF 5, core 3.x) does not.
#if MBEDTLS_VERSION_MAJOR >= 3
  #define SHA_STARTS(c)     mbedtls_sha256_starts((c), 0)
  #define SHA_UPDATE(c,b,n) mbedtls_sha256_update((c), (b), (n))
  #define SHA_FINISH(c,o)   mbedtls_sha256_finish((c), (o))
#else
  #define SHA_STARTS(c)     mbedtls_sha256_starts_ret((c), 0)
  #define SHA_UPDATE(c,b,n) mbedtls_sha256_update_ret((c), (b), (n))
  #define SHA_FINISH(c,o)   mbedtls_sha256_finish_ret((c), (o))
#endif

static void otaRun() {
  otaLastCheckMs = millis();
  otaChecks++;

  // ---- 1. manifest ----
  WiFiClientSecure client;
  client.setCACert(ROOT_CA_BUNDLE);
  HTTPClient http;
  http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS);
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, otaManifestUrl)) { otaSetResult("manifest: begin failed"); return; }
  int code = http.GET();
  if (code != 200) { otaSetResult("manifest: HTTP %d (%s)", code, http.errorToString(code).c_str()); http.end(); return; }
  if (http.getSize() > 4096) { otaSetResult("manifest: too large"); http.end(); return; }
  String body = http.getString();
  http.end();

  // ---- 2. validate ----
  String ver, url, sha, fam;
  if (!jsonFind(body, "version", ver) || !jsonFind(body, "url", url) ||
      !jsonFind(body, "sha256", sha) || !jsonFind(body, "device_family", fam)) {
    otaSetResult("manifest: missing field(s)"); return;
  }
  ver.trim(); url.trim(); sha.trim(); sha.toLowerCase(); fam.trim();
  if (fam != FW_DEVICE_FAMILY)    { otaSetResult("manifest: family '%s' rejected", fam.c_str()); return; }
  if (!url.startsWith("https://")) { otaSetResult("manifest: url is not https"); return; }
  if (!isHex64(sha))              { otaSetResult("manifest: sha256 malformed"); return; }
  int vt[3];
  if (!parseVersion(ver.c_str(), vt)) { otaSetResult("manifest: version unparseable"); return; }

  // ---- 3. compare ----
  int c = cmpVersion(ver.c_str(), FW_VERSION);
  portENTER_CRITICAL(&otaMux);
  strncpy(otaAvailable, c > 0 ? ver.c_str() : "", sizeof(otaAvailable)); otaAvailable[sizeof(otaAvailable) - 1] = 0;
  portEXIT_CRITICAL(&otaMux);
  if (c == 0) { otaSetResult("up to date (%s)", FW_VERSION); return; }
  if (c <  0) { otaSetResult("server offers older %s, ignoring", ver.c_str()); return; }

  // ---- 4. preflight ----
  const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
  if (!target) { otaSetResult("no inactive OTA partition. Partition scheme lacks ota_1"); return; }
  if (ESP.getFreeHeap() < OTA_MIN_FREE_HEAP) { otaSetResult("low heap (%lu), deferring", (unsigned long)ESP.getFreeHeap()); return; }

  // ---- 5. download + hash + write ----
  otaSetResult("downloading %s", ver.c_str());
  WiFiClientSecure dl;
  dl.setCACert(ROOT_CA_BUNDLE);
  HTTPClient h2;
  h2.setConnectTimeout(OTA_HTTP_TIMEOUT_MS);
  h2.setTimeout(OTA_HTTP_TIMEOUT_MS);
  h2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!h2.begin(dl, url)) { otaSetResult("download: begin failed"); return; }
  code = h2.GET();
  if (code != 200) { otaSetResult("download: HTTP %d", code); h2.end(); return; }
  int len = h2.getSize();
  if (len <= 0) { otaSetResult("download: no Content-Length, refusing"); h2.end(); return; }
  if ((size_t)len > target->size) { otaSetResult("download: %d bytes exceeds slot %lu", len, (unsigned long)target->size); h2.end(); return; }
  if (!Update.begin((size_t)len)) { otaSetResult("Update.begin: %s", Update.errorString()); h2.end(); return; }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  SHA_STARTS(&ctx);

  WiFiClient* stream = h2.getStreamPtr();
  static uint8_t buf[2048];
  size_t total = 0;
  unsigned long lastData = millis();
  bool failed = false;
  while (total < (size_t)len) {
    size_t avail = stream->available();
    if (avail) {
      size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
      size_t n = stream->readBytes(buf, want);
      if (n) {
        if (Update.write(buf, n) != n) { otaSetResult("Update.write: %s", Update.errorString()); failed = true; break; }
        SHA_UPDATE(&ctx, buf, n);
        total += n;
        lastData = millis();
      }
    } else {
      if (!stream->connected()) { otaSetResult("download: connection lost at %u/%d", (unsigned)total, len); failed = true; break; }
      if (millis() - lastData > OTA_STALL_TIMEOUT_MS) { otaSetResult("download: stalled at %u/%d", (unsigned)total, len); failed = true; break; }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  h2.end();

  unsigned char digest[32];
  SHA_FINISH(&ctx, digest);
  mbedtls_sha256_free(&ctx);

  if (failed) { Update.abort(); return; }

  // ---- 6. verify integrity before committing ----
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  if (sha != String(hex)) { Update.abort(); otaSetResult("sha256 mismatch, image discarded"); return; }

  if (!Update.end(true)) { otaSetResult("Update.end: %s", Update.errorString()); return; }
  if (!Update.isFinished()) { otaSetResult("Update not finished, aborting"); Update.abort(); return; }

  otaSetResult("installed %s, rebooting", ver.c_str());
  otaRebootPending = true;   // the control loop turns relays off and restarts
}

static void otaTask(void*) {
  otaRun();
  otaBusy = false;
  vTaskDelete(NULL);
}

// Called from the loop task only. Spawns the worker so the loop never blocks.
void otaRequestCheck(bool force) {
  if (otaBusy) return;
  if (wifiState != WS_CONNECTED)      { otaSetResult("skipped: STA not connected"); return; }
  if (otaManifestUrl.length() == 0)   { otaSetResult("skipped: no manifest url configured"); return; }
  if (ESP.getFreeHeap() < OTA_MIN_FREE_HEAP) { otaSetResult("skipped: low heap"); return; }
  otaForceFlag = force;
  otaBusy = true;
  // Core 0 alongside Wi-Fi. Control loop stays on core 1. 16 KB stack for TLS.
  if (xTaskCreatePinnedToCore(otaTask, "ota", 16384, NULL, 1, NULL, 0) != pdPASS) {
    otaBusy = false;
    otaSetResult("could not start OTA task");
  }
}

void handleCheckUpdate() {
  server.send(403, "application/json", "{\"error\":\"OTA disabled in bench build\"}");
}

// ============================================================================
// WATCHDOG. API differs between Arduino-ESP32 core 2.x (IDF 4.4) and 3.x (IDF 5).
// ============================================================================
void wdtSetup() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg;
  cfg.timeout_ms = WDT_TIMEOUT_S * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);   // core 3 usually has TWDT already running
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);   // subscribe the loop task
  Serial.printf("[wdt] task watchdog armed, %lu s\n", (unsigned long)WDT_TIMEOUT_S);
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  // FIRST: relays to their OFF level. Before serial, before NVS, before
  // anything slow. See allRelaysOff() for what this does and does not cover.
  allRelaysOff();

  Serial.begin(115200);
  delay(300);

  initDeviceId();
  loadSystem();
  loadSettings();   // restore targets/bands/modes/order BEFORE the first control pass

  Serial.printf("\n=== %s  fw %s  build %s  id %s  name %s  reset %d ===\n",
                FW_DEVICE_FAMILY, FW_VERSION, FW_BUILD_ID, deviceId, deviceName.c_str(),
                (int)esp_reset_reason());
  Serial.printf("core %d.%d.%d  free heap %lu\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR,
                ESP_ARDUINO_VERSION_PATCH, (unsigned long)ESP.getFreeHeap());
  {
    const esp_partition_t* run = esp_ota_get_running_partition();
    const esp_partition_t* nxt = esp_ota_get_next_update_partition(NULL);
    Serial.printf("running partition %s (%lu KB), OTA target %s\n",
                  run ? run->label : "?", run ? (unsigned long)(run->size / 1024) : 0UL,
                  nxt ? nxt->label : "NONE - partition scheme has no OTA slot");
  }

  Wire.begin(21, 22);
  I2C_two.begin(32, 33);
  sh1.healthy = sensor1.begin(&Wire);    sh1.lastReinitMs = millis();
  sh2.healthy = sensor2.begin(&I2C_two); sh2.lastReinitMs = millis();
  readSensors();   // one validated pass so the first control cycle has real data
  Serial.printf("S1: %s, S2: %s\n", s1_ok ? "OK" : "MISSING", s2_ok ? "OK" : "MISSING");

  TFTSPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(10000000);
  tft.setRotation(0); // portrait: 240x320
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("Booting");
  tft.setTextSize(1);
  tft.setCursor(20, 130);
  tft.print(FW_VERSION);

  wifiSetup();

  server.on("/", handleRoot);
  server.on("/api/status", HTTP_GET,  handleStatus);
  server.on("/api/target", HTTP_POST, handleTarget);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/order",  HTTP_POST, handleOrder);
  server.on(UriBraces("/api/device/{}/{}"), HTTP_POST, handleDevice);
  // new
  server.on("/provision",         HTTP_GET,  handleProvisionPage);
  server.on("/api/wifi/scan",     HTTP_GET,  handleWifiScan);
  server.on("/api/wifi",          HTTP_POST, handleWifiSave);
  server.on("/api/wifi/forget",   HTTP_POST, handleWifiForget);
  server.on("/api/provision",     HTTP_POST, handleProvisionRequest);
  server.on("/api/check-update",  HTTP_POST, handleCheckUpdate);
  server.on("/api/check-update",  HTTP_GET,  handleCheckUpdate);   // convenient from a browser
  server.begin();

  drawDisplay(true);
  wdtSetup();   // last, so nothing in init can trip it
}

void loop() {
  server.handleClient();
  serviceWifi();

  unsigned long now = millis();

  // Read sensors and run the control algorithm once per second. This block
  // has no dependency on anything network related and must stay that way.
  if (now - lastReading >= SENSOR_REFRESH_MS) {
    lastReading = now;
    readSensors();
    updateRelays();
  }

  if (settingsDirty) { settingsDirty = false; saveSettings(); }
  serviceTelemetry(now);

  if (now - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    lastDisplayRefresh = now;
    drawDisplay(false);
  }

  // Scheduled OTA check. Non-blocking: only spawns the worker task.
  if (false && wifiState == WS_CONNECTED && otaNextCheckAt && !otaBusy && otaManifestUrl.length() &&
      (long)(now - otaNextCheckAt) >= 0) {
    otaNextCheckAt = now + (unsigned long)otaIntervalMin * 60000UL;
    if (otaNextCheckAt == 0) otaNextCheckAt = 1;
    otaRequestCheck(false);
  }

  // A verified image is in the inactive slot. Park the outputs, then reboot.
  if (otaRebootPending) {
    allRelaysOff();
    Serial.println("[ota] relays off, restarting into new firmware");
    Serial.flush();
    delay(250);
    ESP.restart();
  }

  esp_task_wdt_reset();
}
