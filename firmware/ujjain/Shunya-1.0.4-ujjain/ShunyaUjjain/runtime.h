#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <time.h>
#include "control.h"
using namespace chamber;
struct SystemConfig {
  char name[49]="Ujjain Chamber 01",ssid[33]="",password[65]="",otaUrl[384]=DEFAULT_OTA_MANIFEST_URL;
  char remoteUrl[256]="",token[160]="";
  uint32_t otaMin=10,remoteSec=60;bool remoteEnabled=false;
};
struct Snapshot {Controller c; uint32_t now=0;uint64_t uptime=0;uint32_t flags=0;bool controlReady=false;};
struct Event {uint64_t uptime=0;int64_t epoch=0;char type[16]="",message[112]="";};
struct Sample {
  uint64_t id=0,uptime=0;int64_t epoch=0;
  float top=NAN,topRH=NAN,bottom=NAN,bottomRH=NAN,average=NAN,rhLow=NAN,rhHigh=NAN;
  uint32_t flags=0,boot=0;int16_t rssi=0;uint8_t state=0,outputs=0;
};
enum class CommandType:uint8_t {CONFIG,MIST,HUM,EXHAUST,OTA_ON,OTA_OFF,SHUTDOWN};
struct Command {CommandType type;Config cfg;};
struct NetJob {uint8_t type;SystemConfig sys;char version[32]="",url[384]="",sha[65]="",logs[2048]="";uint64_t logsThrough=0,revision=0;};
struct NetResult {uint8_t type=0;bool ok=false,available=false;uint64_t logsThrough=0;char message[160]="",version[32]="",url[384]="",sha[65]="",command[1024]="";};
extern SystemConfig sys;
extern Config desired;
extern WebServer server;
extern QueueHandle_t commandQueue,sampleQueue,netQueue,netResults;
extern char deviceId[20],hostName[32],apName[32],apPassword[20],apiKey[33];
extern bool fsOK,apActive,netBusy,otaApproved,updateAvailable;
extern String otaResult;
extern String ntfyResult;
extern NetResult availableUpdate;
extern uint32_t resetStreak;
extern uint64_t lastRevision;
Snapshot snapshot();
void event(const char* type,const char* message);
void notice(const char* message);
void eventsJson(JsonDocument& d);
void saveClimate();
void saveSystem();
bool sendCommand(CommandType type,const Config* c=nullptr);
bool parseClimate(JsonVariantConst data,Config& c,String& error);
void configJson(JsonObject o,const Config& c);
void statusJson(JsonDocument& d,bool secrets=false);
void networkTask(void*);
void loggingBegin();
void loggingService();
void sendHistory();
void sampleJson(JsonObject o,const Sample& s);
String recentSamplesJson();
String remoteSamples(uint64_t after,uint64_t& through);
void wifiService();
void webBegin();
void networkService();
void requestOtaCheck();
void requestOtaInstall();
void requestRestart();
