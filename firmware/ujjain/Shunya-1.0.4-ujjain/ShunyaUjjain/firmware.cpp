#include "firmware.h"
#include "runtime.h"
#include "config_migration.h"
#include "device_identity.h"
#include "status_view.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_ILI9341.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <driver/gpio.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <LittleFS.h>
#include <new>

SystemConfig sys;
Config desired;
WebServer server(80);
QueueHandle_t commandQueue=nullptr,sampleQueue=nullptr,netQueue=nullptr,netResults=nullptr;
char deviceId[20],hostName[32],apName[32],apPassword[20],apiKey[33];
bool apActive=false,netBusy=false,otaApproved=false,updateAvailable=false;
String otaResult="No check yet";
NetResult availableUpdate;
uint64_t lastRevision=0;
uint32_t resetStreak=0;
static portMUX_TYPE dataMux=portMUX_INITIALIZER_UNLOCKED,eventMux=portMUX_INITIALIZER_UNLOCKED;
static Snapshot shared;
static Event events[50];static uint32_t eventCount=0;
static char transient[112]="";static uint32_t transientAt=0;
static bool hotStored=false,fsFailEvent=false,controlStarted=false;
static DNSServer dns;
static uint32_t staTry=0,retryWait=5000,offlineAt=0,connectedAt=0,lastDisplay=0;
static bool wasConnected=false,trying=false,mdnsUp=false,restartPending=false;
static TwoWire bottomBus(1);
static Adafruit_SHT4x* topSensor=nullptr;
static Adafruit_SHT4x* bottomSensor=nullptr;
static SPIClass screenSPI(VSPI);
static Adafruit_ILI9341 tft(&screenSPI,TFT_DC,TFT_CS,TFT_RST);
RTC_DATA_ATTR static uint32_t rtcMagic=0,rtcStreak=0;
static constexpr uint32_t RTC_MAGIC=0x554a4231;

// Load HIGH into all GPIO output latches before enabling any output driver.
// This is the FIRST action of firmwareSetup(), before Serial/NVS/allocations.
// Firmware cannot control pins during ROM/reset. Relay input hardware must
// hold OFF during that interval; confirm with an oscilloscope at commissioning.
static void allRelaysOff(){
  const int pins[]={PIN_HUM,PIN_MIST,PIN_SPARE,PIN_EXHAUST,PIN_HEAT};
  for(int p:pins)gpio_set_level((gpio_num_t)p,1);
  for(int p:pins)gpio_set_direction((gpio_num_t)p,GPIO_MODE_OUTPUT);
}
static void writeOutputs(const Outputs& o){
  // Break before make: first de-energize every unwanted load, then energize.
  if(!o.heat)gpio_set_level((gpio_num_t)PIN_HEAT,1);
  if(!o.hum)gpio_set_level((gpio_num_t)PIN_HUM,1);
  if(!o.mist)gpio_set_level((gpio_num_t)PIN_MIST,1);
  if(!o.exhaust)gpio_set_level((gpio_num_t)PIN_EXHAUST,1);
  gpio_set_level((gpio_num_t)PIN_SPARE,1);
  if(o.heat)gpio_set_level((gpio_num_t)PIN_HEAT,0);
  if(o.hum&&!o.heat)gpio_set_level((gpio_num_t)PIN_HUM,0);
  if(o.mist&&!o.heat&&!o.hum)gpio_set_level((gpio_num_t)PIN_MIST,0);
  if(o.exhaust&&!o.heat&&!o.mist&&!o.hum)gpio_set_level((gpio_num_t)PIN_EXHAUST,0);
}
Snapshot snapshot(){Snapshot s;portENTER_CRITICAL(&dataMux);s=shared;portEXIT_CRITICAL(&dataMux);return s;}
void event(const char* type,const char* msg){
  Event e;e.uptime=esp_timer_get_time()/1000;time_t t=time(nullptr);e.epoch=t>1700000000?t:0;
  strlcpy(e.type,type,sizeof(e.type));strlcpy(e.message,msg,sizeof(e.message));
  portENTER_CRITICAL(&eventMux);events[eventCount%50]=e;++eventCount;portEXIT_CRITICAL(&eventMux);
}
void notice(const char* msg){portENTER_CRITICAL(&eventMux);strlcpy(transient,msg,sizeof(transient));transientAt=millis();portEXIT_CRITICAL(&eventMux);}
void eventsJson(JsonDocument& d){
  static Event copy[50];uint32_t count; // Main-task scratch; keep 7 KB off the loop stack.
  portENTER_CRITICAL(&eventMux);count=std::min<uint32_t>(eventCount,50U);for(uint32_t i=0;i<count;++i)copy[i]=events[(eventCount-count+i)%50];portEXIT_CRITICAL(&eventMux);
  JsonArray a=d["events"].to<JsonArray>();for(uint32_t i=0;i<count;++i){auto o=a.add<JsonObject>();o["uptime_ms"]=copy[i].uptime;o["epoch"]=copy[i].epoch;o["type"]=copy[i].type;o["message"]=copy[i].message;}
}
bool sendCommand(CommandType type,const Config* c){Command cmd;cmd.type=type;if(c)cmd.cfg=*c;return commandQueue&&xQueueSend(commandQueue,&cmd,0)==pdTRUE;}
void saveClimate(){Preferences p;if(p.begin("uj-climate",false)){p.putUInt("schema",SCHEMA);p.putBytes("cfg",&desired,sizeof(desired));p.putUInt("crc",checksum(&desired,sizeof(desired)));p.end();}}
void saveSystem(){Preferences p;if(p.begin("uj-system",false)){p.putUInt("schema",SYSTEM_SCHEMA);p.putBytes("cfg",&sys,sizeof(sys));p.putUInt("crc",checksum(&sys,sizeof(sys)));p.end();}}
static bool httpsOrEmpty(const char* p){return !*p||strncmp(p,"https://",8)==0;}
static void loadSettings(){
  Preferences p;
  bool migrated=false;
  if(p.begin("uj-climate",true)){
    uint8_t blob[sizeof(Config)];size_t size=p.getBytesLength("cfg");uint32_t schema=p.getUInt("schema");Config c;
    if(size<=sizeof(blob)&&p.getBytes("cfg",blob,size)==size&&decodeClimate(schema,blob,size,p.getUInt("crc"),c)){desired=c;migrated=schema==1;}
    p.end();
  }
  if(migrated){saveClimate();event("SETTINGS","Climate schema 1 migrated to 2; previous values retained");}
  if(p.begin("uj-system",true)){
    SystemConfig c;
    if(p.getUInt("schema")==SYSTEM_SCHEMA&&p.getBytesLength("cfg")==sizeof(c)&&p.getBytes("cfg",&c,sizeof(c))==sizeof(c)&&p.getUInt("crc")==checksum(&c,sizeof(c))){
      c.name[48]=0;c.ssid[32]=0;c.password[64]=0;c.otaUrl[383]=0;c.remoteUrl[255]=0;c.token[159]=0;
      if(c.otaMin>=1&&c.otaMin<=1440&&c.remoteSec>=30&&c.remoteSec<=3600&&httpsOrEmpty(c.otaUrl)&&httpsOrEmpty(c.remoteUrl))sys=c;
    }p.end();
  }
  if(p.begin("uj-safety",true)){hotStored=p.getBool("hot",false);lastRevision=p.getULong64("revision",0);p.end();}
}
static bool initSensor(Adafruit_SHT4x*& sensor,TwoWire& bus){
  // Adafruit SHT4x 1.0.5 begin() deletes internal helpers without nulling
  // them before an early failure. Never call begin() twice on one instance.
  delete sensor;sensor=new(std::nothrow) Adafruit_SHT4x();
  if(!sensor||!sensor->begin(&bus))return false;
  sensor->setPrecision(SHT4X_HIGH_PRECISION);sensor->setHeater(SHT4X_NO_HEATER);return true;
}
static void readOne(Adafruit_SHT4x*& sensor,TwoWire& bus,Sensor& health,bool& begun,uint32_t& lastTry,uint32_t now,float tOffset,float rhOffset){
  if(!begun&&elapsed(now,lastTry)>=SENSOR_RETRY_MS){lastTry=now;begun=initSensor(sensor,bus);}
  sensors_event_t rh,t;bool ok=begun&&sensor->getEvent(&rh,&t);
  health.ingest(ok,ok?t.temperature:NAN,ok?rh.relative_humidity:NAN,now,tOffset,rhOffset);
  if(health.bad>=3)begun=false;
}
static uint32_t flagBits(const Controller& c,uint32_t n){
  return (c.hot?1:0)|(!c.top.usable(n)?2:0)|(!c.bottom.usable(n)?4:0)|(c.purgeIneffective?8:0)|(c.mistPending?16:0)|(c.disagreement?32:0);
}
static void controlTask(void*){
  esp_task_wdt_add(nullptr);
  Controller c(millis());c.cfg=desired;c.hot=hotStored;
  Wire.begin(21,22,100000);bottomBus.begin(32,33,100000);Wire.setTimeOut(30);bottomBus.setTimeOut(30);
  bool tBegin=initSensor(topSensor,Wire),bBegin=initSensor(bottomSensor,bottomBus);
  uint32_t tTry=millis(),bTry=millis(),lastRead=millis()-SENSOR_MS,lastSample=millis();
  uint32_t bootId=esp_random();double sums[7]={};uint32_t counts[7]={};uint32_t aggregateFlags=0;uint8_t aggregateOutputs=0;
  TickType_t wake=xTaskGetTickCount();bool oldHot=c.hot;uint32_t oldFlags=0;
  for(;;){
    uint32_t n=millis();Command cmd;
    while(xQueueReceive(commandQueue,&cmd,0)==pdTRUE){
      switch(cmd.type){
        case CommandType::CONFIG:if(validConfig(cmd.cfg)){
          if(cmd.cfg.top_t_offset!=c.cfg.top_t_offset||cmd.cfg.top_rh_offset!=c.cfg.top_rh_offset||cmd.cfg.bottom_t_offset!=c.cfg.bottom_t_offset||cmd.cfg.bottom_rh_offset!=c.cfg.bottom_rh_offset)lastRead=n-SENSOR_MS;
          c.applyConfig(cmd.cfg,n);
        }break;
        case CommandType::MIST:c.requestMist();break;
        case CommandType::HUM:c.requestHum(n);break;
        case CommandType::EXHAUST:c.requestExhaust();break;
        case CommandType::OTA_ON:c.ota=true;break;
        case CommandType::OTA_OFF:c.ota=false;break;
        case CommandType::SHUTDOWN:c.shutdown=true;break;
      }
    }
    bool sampled=elapsed(n,lastRead)>=SENSOR_MS;
    if(sampled){
      lastRead=n;bool oldT=c.top.healthy,oldB=c.bottom.healthy;
      readOne(topSensor,Wire,c.top,tBegin,tTry,n,c.cfg.top_t_offset,c.cfg.top_rh_offset);readOne(bottomSensor,bottomBus,c.bottom,bBegin,bTry,n,c.cfg.bottom_t_offset,c.cfg.bottom_rh_offset);
      if(oldT!=c.top.healthy){event(c.top.healthy?"NOTICE":"FAULT",c.top.healthy?"TOP sensor recovered / qualified":"TOP sensor offline - heating disabled");if(c.top.healthy)notice("SENSOR RECOVERED - TOP");}
      if(oldB!=c.bottom.healthy){event(c.bottom.healthy?"NOTICE":"FAULT",c.bottom.healthy?"BOTTOM sensor recovered / qualified":"BOTTOM sensor offline - heating disabled");if(c.bottom.healthy)notice("SENSOR RECOVERED - BOTTOM");}
    }
    State before=c.state;Outputs oldOut=c.out;c.tick(n);writeOutputs(c.out);
    if(before!=c.state){event("STATE",c.reason);if(before==State::MISTING)notice("MIST COMPLETE");if(before==State::HEATING&&c.average(n)>=c.cfg.temperature_target)notice("TARGET REACHED");}
    if(oldOut.heat&&!c.out.heat)event("OUTPUT","Heater OFF");
    if(!oldOut.heat&&c.out.heat)event("OUTPUT","Heater ON");
    if(c.hot!=oldHot){event(c.hot?"FAULT":"NOTICE",c.hot?"HIGH TEMPERATURE - heater locked out":"Temperature recovered for 120 seconds");oldHot=c.hot;}
    uint32_t flags=flagBits(c,n);if((flags&16)&&!(oldFlags&16))event("ADVISORY","Mist pending - waiting for safe conditions");if((flags&8)&&!(oldFlags&8))event("ADVISORY","HIGH HUMIDITY - PURGE INEFFECTIVE");oldFlags=flags;
    Snapshot s;s.c=c;s.now=n;s.uptime=esp_timer_get_time()/1000;s.flags=flags;s.controlReady=true;
    portENTER_CRITICAL(&dataMux);shared=s;portEXIT_CRITICAL(&dataMux);
    if(sampled){
      float values[]={c.top.usable(n)?c.top.t:NAN,c.top.usable(n)?c.top.rh:NAN,c.bottom.usable(n)?c.bottom.t:NAN,c.bottom.usable(n)?c.bottom.rh:NAN,c.average(n),c.low(n),c.high(n)};
      for(int i=0;i<7;++i)if(std::isfinite(values[i])){sums[i]+=values[i];++counts[i];}
      aggregateFlags|=flags;aggregateOutputs|=(c.out.heat?1:0)|(c.out.hum?2:0)|(c.out.mist?4:0)|(c.out.exhaust?8:0);
    }
    if(elapsed(n,lastSample)>=60000){
      lastSample=n;Sample row;row.uptime=s.uptime;time_t epoch=time(nullptr);row.epoch=epoch>1700000000?epoch:0;row.boot=bootId;
      float v[7];for(int i=0;i<7;++i){v[i]=counts[i]?sums[i]/counts[i]:NAN;sums[i]=0;counts[i]=0;}
      row.top=v[0];row.topRH=v[1];row.bottom=v[2];row.bottomRH=v[3];row.average=v[4];row.rhLow=v[5];row.rhHigh=v[6];
      row.flags=aggregateFlags;row.outputs=aggregateOutputs;aggregateFlags=0;aggregateOutputs=0;row.state=(uint8_t)c.state;
      if(xQueueSend(sampleQueue,&row,0)!=pdTRUE)event("ADVISORY","History buffer full; a minute sample was dropped");
    }
    // Clear boot-loop RTC counter after ten stable minutes. NVS only on changes.
    if(elapsed(n,c.bootAt)>600000)rtcStreak=0;
    esp_task_wdt_reset();vTaskDelayUntil(&wake,pdMS_TO_TICKS(50));
  }
}
static void startAP(){
  if(apActive)return;
  WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  if(WiFi.softAP(apName,apPassword)){apActive=true;dns.start(53,"*",IPAddress(192,168,4,1));event("WIFI","Setup access point started");}
}
void wifiService(){
  uint32_t n=millis();bool connected=WiFi.status()==WL_CONNECTED;
  if(connected&&!wasConnected){connectedAt=n;trying=false;retryWait=5000;configTzTime("IST-5:30","pool.ntp.org","time.google.com");
    mdnsUp=MDNS.begin(hostName);if(mdnsUp)MDNS.addService("http","tcp",80);event("WIFI","Wi-Fi restored");notice("WI-FI RESTORED");}
  if(!connected&&wasConnected){offlineAt=n;trying=false;staTry=n;retryWait=5000;if(mdnsUp){MDNS.end();mdnsUp=false;}event("WIFI","Wi-Fi lost; climate control continues");}
  wasConnected=connected;
  if(!connected){
    if(!sys.ssid[0]||elapsed(n,offlineAt)>=AP_AFTER_MS)startAP();
    if(trying&&elapsed(n,staTry)>=20000){WiFi.disconnect(false,false);trying=false;staTry=n;retryWait=std::min<uint32_t>(retryWait*2,60000U);}
    if(sys.ssid[0]&&!trying&&elapsed(n,staTry)>=retryWait){WiFi.begin(sys.ssid,sys.password);staTry=n;trying=true;}
  }else if(apActive&&elapsed(n,connectedAt)>=AP_LINGER_MS){dns.stop();WiFi.softAPdisconnect(true);apActive=false;WiFi.mode(WIFI_STA);event("WIFI","Setup AP closed after successful connection");}
  if(apActive)dns.processNextRequest();
}
// Shared banner priority for TFT and web; read the transient under its mutex.
static void bannerFor(const Snapshot& s,char* text,size_t size,const char*& kind){
  const auto& c=s.c;const char* msg="";kind="";
  if(c.hot){msg="HIGH TEMPERATURE - HEATER LOCKED";kind="fault";}
  else if(!c.any(s.now)){msg="BOTH SENSORS OFFLINE / VALIDATING";kind="fault";}
  else {
    kind="advisory";
    if(!c.both(s.now))msg="SENSOR DEGRADED - HEAT DISABLED";
    else if(!c.heatReady(s.now))msg="SENSOR READ GLITCH - HEAT DISABLED";
    else if(c.purgeIneffective)msg="HIGH RH - PURGE INEFFECTIVE";
    else if(c.mistPending)msg="MIST DELAYED - REQUEST RETAINED";
    else if(c.disagreement)msg="SENSOR DISAGREEMENT";
    else if(resetStreak>=3)msg="REPEATED RESETS - CHECK POWER";
    else if(!fsOK)msg="PERSISTENT HISTORY UNAVAILABLE";
    else if(WiFi.status()!=WL_CONNECTED)msg="WI-FI OFFLINE - CONTROL CONTINUES";
    else if(updateAvailable)msg="UPDATE AVAILABLE - OPEN SYSTEM";
    else {
      kind="notice";portENTER_CRITICAL(&eventMux);
      strlcpy(text,elapsed(s.now,transientAt)<5000?transient:"",size);
      portEXIT_CRITICAL(&eventMux);if(!text[0])kind="";return;
    }
  }
  strlcpy(text,msg,size);
}
#include "tft_ui.h"
void requestRestart(){if(sendCommand(CommandType::SHUTDOWN)){restartPending=true;event("SYSTEM","Restart scheduled after heater cooldown");}}
void firmwareSetup(){
  allRelaysOff(); // MUST REMAIN FIRST.
  Serial.begin(115200);
  uint64_t mac=ESP.getEfuseMac();char suffix[5];formatDeviceIdentity(mac,deviceId,suffix);
  snprintf(hostName,sizeof(hostName),"shunya-%s",suffix);snprintf(apName,sizeof(apName),"SHUNYA-UJ-%s",suffix);
  snprintf(apPassword,sizeof(apPassword),"Shunya%s",suffix);
  // Per-device random local control code survives OTA; visible on the TFT.
  Preferences auth;auth.begin("uj-auth",false);String key=auth.getString("key","");if(key.length()!=8){char k[9];snprintf(k,sizeof(k),"%08X",esp_random());key=k;auth.putString("key",key);}strlcpy(apiKey,key.c_str(),sizeof(apiKey));auth.end();
  Serial.printf("Local control code: %s\n",apiKey);
  loadSettings();
  esp_reset_reason_t reason=esp_reset_reason();bool unexpected=reason==ESP_RST_PANIC||reason==ESP_RST_INT_WDT||reason==ESP_RST_TASK_WDT||reason==ESP_RST_WDT||reason==ESP_RST_BROWNOUT;
  if(rtcMagic!=RTC_MAGIC){rtcMagic=RTC_MAGIC;rtcStreak=0;}if(unexpected)++rtcStreak;else rtcStreak=0;resetStreak=rtcStreak;
  char bootMsg[100];snprintf(bootMsg,sizeof(bootMsg),"Boot %s; reset reason %d; recent unexpected resets %lu",FW_VERSION,(int)reason,(unsigned long)resetStreak);event("SYSTEM",bootMsg);
  Serial.printf("\n%s %s %s\n",FW_DEVICE_FAMILY,FW_VERSION,deviceId);Serial.printf("Local control code: %s  AP: %s / %s\n",apiKey,apName,apPassword);
  const auto* run=esp_ota_get_running_partition();const auto* target=esp_ota_get_next_update_partition(nullptr);
  Serial.printf("Running partition: %s; OTA target: %s\n",run?run->label:"?",target?target->label:"NONE - choose two OTA slots");
  commandQueue=xQueueCreate(12,sizeof(Command));sampleQueue=xQueueCreate(16,sizeof(Sample));netQueue=xQueueCreate(1,sizeof(NetJob));netResults=xQueueCreate(2,sizeof(NetResult));
  if(!commandQueue||!sampleQueue||!netQueue||!netResults){Serial.println("FATAL: queue allocation failed; outputs OFF");for(;;)delay(1000);}
  esp_task_wdt_config_t wdt={};wdt.timeout_ms=WDT_SECONDS*1000;wdt.idle_core_mask=0;wdt.trigger_panic=true;
  if(esp_task_wdt_reconfigure(&wdt)!=ESP_OK)esp_task_wdt_init(&wdt);
  if(xTaskCreatePinnedToCore(controlTask,"climate",8192,nullptr,3,nullptr,1)!=pdPASS){Serial.println("FATAL: climate task failed; outputs OFF");for(;;)delay(1000);}controlStarted=true;
  // Filesystem, display and networking cannot delay the independent climate task.
  loggingBegin();
  screenSPI.begin(TFT_SCK,TFT_MISO,TFT_MOSI,TFT_CS);tft.begin(TFT_HZ);tft.setRotation(0);
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);WiFi.setHostname(hostName);offlineAt=millis();staTry=millis()-retryWait;
  if(!sys.ssid[0])startAP();
  if(xTaskCreatePinnedToCore(networkTask,"https",16384,nullptr,1,nullptr,0)!=pdPASS){event("FAULT","HTTPS task unavailable; OTA/remote disabled");otaResult="HTTPS task could not start";}
  webBegin();Snapshot first=snapshot();drawDisplay(first);
}
void firmwareLoop(){
  wifiService();server.handleClient();networkService();loggingService();
  Snapshot s=snapshot();
  if(s.controlReady&&s.c.hot!=hotStored){hotStored=s.c.hot;Preferences p;p.begin("uj-safety",false);p.putBool("hot",hotStored);p.end();}
  if(restartPending&&s.controlReady&&s.c.shutdown&&s.c.safeToReboot(s.now)){event("SYSTEM","Restarting; outputs OFF");delay(100);ESP.restart();}
  if(elapsed(millis(),lastDisplay)>=120){lastDisplay=millis();drawDisplay(s);}
  if(!fsOK&&!fsFailEvent){event("ADVISORY","Persistent history unavailable; RAM logging continues");fsFailEvent=true;}
  delay(2);
}
void configJson(JsonObject o,const Config& c){
#define C(x) o[#x]=c.x
 C(top_t_offset);C(top_rh_offset);C(bottom_t_offset);C(bottom_rh_offset);
 o["humidity_reducer"]=c.humidity_reducer==HumidityReducer::DRIEST?"driest":"wettest";
 C(temperature_target);C(temperature_band);C(top_margin);C(humidity_target);C(humidity_band);C(post_heat_sec);C(post_mist_sec);C(hum_max_sec);C(mist_period_min);C(mist_duration_sec);C(fae_period_min);C(fae_duration_sec);C(high_rh);C(preheat_rh);C(purge_max_sec);C(purge_cooldown_sec);
#undef C
 o["heater"]=c.heater==Mode::AUTO?"AUTO":"OFF";o["humidifier"]=c.humidifier==Mode::AUTO?"AUTO":"OFF";o["mist"]=c.mist==Mode::AUTO?"AUTO":"OFF";o["exhaust"]=c.exhaust==Mode::AUTO?"AUTO":"OFF";
}
void statusJson(JsonDocument& d,bool secrets){
 Snapshot s=snapshot();const Controller& c=s.c;
 d["device_id"]=deviceId;d["name"]=sys.name;d["version"]=FW_VERSION;d["banner"]=FW_BANNER;d["family"]=FW_DEVICE_FAMILY;d["uptime_ms"]=s.uptime;d["state"]=stateName(c.state);d["reason"]=c.reason;d["notice"]="";
 char note[112];portENTER_CRITICAL(&eventMux);strlcpy(note,elapsed(millis(),transientAt)<5000?transient:"",sizeof(note));portEXIT_CRITICAL(&eventMux);d["notice"]=note;
 auto sensor=[&](const char* name,const Sensor& v){auto o=d[name].to<JsonObject>();bool valid=v.usable(s.now);o["healthy"]=v.healthy;o["fresh"]=v.fresh;o["valid"]=valid;o["temperature"]=valid?v.t:NAN;o["rh"]=valid?v.rh:NAN;o["consecutive_good"]=v.good;o["consecutive_bad"]=v.bad;};
 sensor("top",c.top);sensor("bottom",c.bottom);d["average"]=c.average(s.now);d["rh_low"]=c.low(s.now);d["rh_high"]=c.high(s.now);d["temperature_spread"]=c.both(s.now)?fabs(c.top.t-c.bottom.t):NAN;d["rh_spread"]=c.both(s.now)?fabs(c.top.rh-c.bottom.rh):NAN;
 auto o=d["outputs"].to<JsonObject>();o["heater"]=c.out.heat;o["humidifier"]=c.out.hum;o["mist"]=c.out.mist;o["exhaust"]=c.out.exhaust;
 const StatusView view=statusView(c,s.now);
#define V(x) d[#x]=view.x
 V(heat_lock_remaining_sec);V(water_lock_remaining_sec);V(state_elapsed_sec);V(mist_remaining_sec);V(hum_runtime_remaining_sec);V(purge_remaining_sec);V(purge_cooldown_remaining_sec);
 V(heat_runtime_remaining_sec);V(fae_remaining_sec);V(hum_pause_remaining_sec);V(preheat_settle_remaining_sec);
#undef V
 auto blocked=d["blocked_by"].to<JsonObject>();const char* actuator[]={"heater","humidifier","mist","exhaust"};
 for(int i=0;i<4;++i){if(view.blocked_by[i])blocked[actuator[i]]=view.blocked_by[i];else blocked[actuator[i]]=nullptr;}
 d["manual_mist"]=c.manualMist;
 char bannerText[112];const char* bannerKind;bannerFor(s,bannerText,sizeof(bannerText),bannerKind);
 auto banner=d["banner_message"].to<JsonObject>();banner["text"]=bannerText;banner["kind"]=bannerKind;
 d["heat_pending"]=c.heatPending;d["mist_pending"]=c.mistPending;d["fae_pending"]=c.faePending;
 d["mist_next_sec"]=c.cfg.mist_period_min?std::max<int64_t>(0,(int64_t)c.cfg.mist_period_min*60-elapsed(s.now,c.mistDueAt)/1000):-1;
 d["water_locked"]=c.waterLocked(s.now);d["heat_locked"]=c.heatLocked(s.now);d["manual_humidifier"]=c.manualHum;d["manual_exhaust"]=c.manualExhaust;
 JsonArray faults=d["faults"].to<JsonArray>();if(c.hot)faults.add("HIGH TEMPERATURE - HEATER LOCKED OUT");if(!c.any(s.now))faults.add("BOTH SENSORS OFFLINE / VALIDATING - ALL OUTPUTS DISABLED");
 JsonArray advice=d["advisories"].to<JsonArray>();if(c.any(s.now)&&!c.both(s.now))advice.add("SENSOR DEGRADED - HEATING DISABLED; WATER/EXHAUST USE REMAINING SENSOR");else if(c.both(s.now)&&!c.heatReady(s.now))advice.add("SENSOR READ GLITCH - HEATING DISABLED UNTIL FRESH READINGS");if(c.purgeIneffective)advice.add("HIGH HUMIDITY - PURGE INEFFECTIVE. Ambient humidity may be high. Check room humidity and exhaust airflow.");if(c.mistPending)advice.add("MIST DELAYED - request retained");if(c.disagreement)advice.add("SENSOR DISAGREEMENT - inspect placement");if(resetStreak>=3)advice.add("REPEATED RESETS - inspect power and diagnostics");if(!fsOK)advice.add("PERSISTENT HISTORY UNAVAILABLE");
 bool online=WiFi.status()==WL_CONNECTED;if(!online)advice.add("WI-FI OFFLINE - chamber control continues normally");if(updateAvailable)advice.add("UPDATE AVAILABLE - installation requires approval");if(!netBusy&&(ESP.getFreeHeap()<LOW_HEAP_WARN||ESP.getMaxAllocHeap()<LOW_BLOCK_WARN))advice.add("LOW MEMORY - updates and notifications may fail");
 configJson(d["config"].to<JsonObject>(),c.cfg);configJson(d["saved_config"].to<JsonObject>(),desired);
 auto w=d["wifi"].to<JsonObject>();w["connected"]=online;w["ip"]=online?WiFi.localIP().toString():"";w["ssid"]=sys.ssid;w["rssi"]=online?WiFi.RSSI():0;w["ap_active"]=apActive;w["ap_name"]=apName;w["hostname"]=hostName;
 const auto* run=esp_ota_get_running_partition();const auto* target=esp_ota_get_next_update_partition(nullptr);auto u=d["ota"].to<JsonObject>();u["running_partition"]=run?run->label:"?";u["target_partition"]=target?target->label:"NONE - OTA unavailable";u["slot_bytes"]=target?target->size:0;u["available"]=updateAvailable;u["version"]=availableUpdate.version;u["result"]=otaResult;u["busy"]=netBusy;u["approved"]=otaApproved;
 d["reset_reason"]=(int)esp_reset_reason();d["reset_streak"]=resetStreak;d["free_heap"]=ESP.getFreeHeap();d["min_heap"]=ESP.getMinFreeHeap();d["max_alloc_heap"]=ESP.getMaxAllocHeap();d["ntfy"]=ntfyResult;d["core_version"]=ESP_ARDUINO_VERSION_STR;d["history_persistent"]=fsOK;d["last_remote_revision"]=lastRevision;d["hard_temperature_limit"]=HARD_TEMP_C;
 if(secrets){auto settings=d["system"].to<JsonObject>();settings["ota_url"]=sys.otaUrl;settings["ota_min"]=sys.otaMin;settings["remote_enabled"]=sys.remoteEnabled;settings["remote_url"]=sys.remoteUrl;settings["remote_sec"]=sys.remoteSec;settings["token_configured"]=sys.token[0]!=0;}
}
