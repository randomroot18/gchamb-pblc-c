#include "runtime.h"
#include "web_ui.h"
#include <esp_system.h>
#include <LittleFS.h>
static bool authorized(){return server.header("X-Shunya-Key")==apiKey;}
static void reply(int code,const String& msg){JsonDocument d;d["ok"]=code<300;d["message"]=msg;String text;serializeJson(d,text);server.send(code,"application/json",text);}
static bool requireAuth(){if(authorized())return true;reply(401,"Enter the local control code shown on the chamber screen");return false;}
// Stream JSON into a fixed buffer: WebServer's default plain-body parser
// otherwise allocates the entire attacker-supplied Content-Length first.
static char requestBody[4097];
static bool rawReady=false;
class BoundedJsonHandler: public RequestHandler {
  String route;WebServer::THandlerFunction fn;bool auth;bool reject=false;size_t length=0;
public:
  BoundedJsonHandler(const char* r,WebServer::THandlerFunction f,bool a):route(r),fn(f),auth(a){}
  bool canHandle(WebServer&,HTTPMethod method,const String& uri) override{bool match=method==HTTP_POST&&(route=="*"||uri==route);if(match){rawReady=false;reject=false;}return match;}
  bool canRaw(WebServer&,const String&) override{return true;}
  void raw(WebServer& web,const String&,HTTPRaw& data) override{
    if(data.status==RAW_START){length=0;requestBody[0]=0;rawReady=false;reject=false;
      if(web.clientContentLength()<0||web.clientContentLength()>4096){reply(413,"JSON body too large");reject=true;}
      else if(!web.header("Content-Type").startsWith("application/json")){reply(415,"Use application/json");reject=true;}
      else if(auth&&!authorized()){reply(401,"Local control code required");reject=true;}
      if(reject)web.client().stop();
    }else if(data.status==RAW_WRITE&&!reject){
      if(length+data.currentSize>4096){reject=true;web.client().stop();return;}
      memcpy(requestBody+length,data.buf,data.currentSize);length+=data.currentSize;requestBody[length]=0;
    }else if(data.status==RAW_END&&!reject)rawReady=true;
  }
  bool handle(WebServer&,HTTPMethod,const String&) override{
    if(!reject){if(!rawReady){reply(415,"Use a JSON request body");return true;}fn();}return true;
  }
};
// Access policy: anyone on the chamber network may view and change climate settings.
// The local control code (TFT footer / Serial) guards only network, firmware and reset actions.
static void onPost(const char* route,WebServer::THandlerFunction fn,bool auth=true){server.addHandler(new BoundedJsonHandler(route,fn,auth));}
static bool body(JsonDocument& d,bool auth=true){if(auth&&!requireAuth())return false;if(!rawReady||deserializeJson(d,requestBody)||!d.is<JsonObject>()){reply(400,"Send a JSON object (maximum 4096 bytes)");return false;}return true;}
static bool writable(){if(otaApproved||snapshot().c.shutdown){reply(409,"Update/restart safety hold active");return false;}return true;}
static bool number(JsonVariantConst value,float& out){if(!(value.is<float>()||value.is<int>()||value.is<unsigned>()))return false;out=value.as<float>();return std::isfinite(out);}
bool parseClimate(JsonVariantConst data,Config& c,String& error){
 if(!data.is<JsonObjectConst>()){error="Configuration must be an object";return false;}
 for(JsonPairConst pair:data.as<JsonObjectConst>()){
   const char* k=pair.key().c_str();float f;
#define FLOAT_FIELD(x) if(!strcmp(k,#x)){if(!number(pair.value(),f)){error="Invalid numeric " #x;return false;}c.x=f;continue;}
#define UINT_FIELD(x) if(!strcmp(k,#x)){if(!number(pair.value(),f)||f<0||f>100000||floorf(f)!=f){error="Invalid integer " #x;return false;}c.x=(uint32_t)f;continue;}
   FLOAT_FIELD(top_t_offset) FLOAT_FIELD(top_rh_offset) FLOAT_FIELD(bottom_t_offset) FLOAT_FIELD(bottom_rh_offset)
   FLOAT_FIELD(temperature_target) FLOAT_FIELD(temperature_band) FLOAT_FIELD(top_margin) FLOAT_FIELD(humidity_target) FLOAT_FIELD(humidity_band) FLOAT_FIELD(high_rh) FLOAT_FIELD(preheat_rh)
   UINT_FIELD(post_heat_sec) UINT_FIELD(post_mist_sec) UINT_FIELD(hum_max_sec) UINT_FIELD(mist_period_min) UINT_FIELD(mist_duration_sec) UINT_FIELD(fae_period_min) UINT_FIELD(fae_duration_sec) UINT_FIELD(purge_max_sec) UINT_FIELD(purge_cooldown_sec)
#undef FLOAT_FIELD
#undef UINT_FIELD
   if(!strcmp(k,"humidity_reducer")){const char* reducer=pair.value().as<const char*>();if(!reducer||(strcmp(reducer,"driest")&&strcmp(reducer,"wettest"))){error="humidity_reducer must be driest or wettest";return false;}c.humidity_reducer=!strcmp(reducer,"driest")?HumidityReducer::DRIEST:HumidityReducer::WETTEST;continue;}
#define MODE_FIELD(x) if(!strcmp(k,#x)){const char* m=pair.value().as<const char*>();if(!m||(strcmp(m,"AUTO")&&strcmp(m,"OFF"))){error="Mode must be AUTO or OFF";return false;}c.x=!strcmp(m,"AUTO")?Mode::AUTO:Mode::OFF;continue;}
   MODE_FIELD(heater) MODE_FIELD(humidifier) MODE_FIELD(mist) MODE_FIELD(exhaust)
#undef MODE_FIELD
   error=String("Unknown climate field: ")+k;return false;
 }
 if(!validConfig(c)){error="Setting outside allowed range or preheat RH must be below high RH";return false;}return true;
}
static bool commitClimate(const Config& c){if(!sendCommand(CommandType::CONFIG,&c)){reply(503,"Control command queue full; retry");return false;}desired=c;saveClimate();notice("SETTINGS SAVED");event("SETTINGS","Climate settings saved");return true;}
static void climateHandler(){JsonDocument d;if(!body(d,false)||!writable())return;Config c=desired;String error;if(!parseClimate(d.as<JsonVariantConst>(),c,error)){reply(400,error);return;}if(commitClimate(c))reply(200,"Settings saved");}
static void deviceHandler(){
 JsonDocument d;if(!body(d,false)||!writable())return;
 String uri=server.uri();int split=uri.lastIndexOf('/');String action=uri.substring(split+1);String dev=uri.substring(strlen("/api/device/"),split);
 Config c=desired;CommandType type=CommandType::CONFIG;
 if(dev=="heater"){if(action=="AUTO")c.heater=Mode::AUTO;else if(action=="OFF")c.heater=Mode::OFF;else {reply(400,"Heater supports AUTO / OFF only");return;}}
 else if(dev=="humidifier"){if(action=="AUTO"||action=="ON")c.humidifier=Mode::AUTO;else if(action=="OFF")c.humidifier=Mode::OFF;else{reply(400,"Unknown humidifier action");return;}if(action=="ON")type=CommandType::HUM;}
 else if(dev=="mist"){if(action=="AUTO"||action=="MIST")c.mist=Mode::AUTO;else if(action=="OFF")c.mist=Mode::OFF;else{reply(400,"Mist supports AUTO / MIST / OFF");return;}if(action=="MIST")type=CommandType::MIST;}
 else if(dev=="exhaust"){if(action=="AUTO"||action=="ON")c.exhaust=Mode::AUTO;else if(action=="OFF")c.exhaust=Mode::OFF;else{reply(400,"Unknown exhaust action");return;}if(action=="ON")type=CommandType::EXHAUST;}
 else{reply(404,"Unknown actuator");return;}
 if(uxQueueSpacesAvailable(commandQueue)<2){reply(503,"Command queue busy");return;}
 // AUTO explicitly cancels the transient manual request by applying OFF then AUTO.
 if(action=="AUTO"&&dev!="heater"){Config off=c;if(dev=="humidifier")off.humidifier=Mode::OFF;if(dev=="mist")off.mist=Mode::OFF;if(dev=="exhaust")off.exhaust=Mode::OFF;sendCommand(CommandType::CONFIG,&off);}
 if(commitClimate(c)){if(type!=CommandType::CONFIG)sendCommand(type);reply(200,"Requested; local safety interlocks apply");}
}
static bool safeText(const char* s,size_t max){if(!s||strlen(s)>max)return false;for(const char* p=s;*p;++p)if((unsigned char)*p<32)return false;return true;}
static void systemSettings(){
 JsonDocument d;if(!body(d)||!writable())return;SystemConfig next=sys;
 for(JsonPair p:d.as<JsonObject>()){
   String key=p.key().c_str();
   if(key=="ota_url"||key=="remote_url"||key=="token"){
     const char* v=p.value().as<const char*>();size_t limit=key=="ota_url"?383:key=="remote_url"?255:159;
     if(!safeText(v,limit)||(key!="token"&&v[0]&&strncmp(v,"https://",8))){reply(400,"Invalid HTTPS URL/token");return;}
     if(key=="ota_url")strlcpy(next.otaUrl,v,sizeof(next.otaUrl));if(key=="remote_url")strlcpy(next.remoteUrl,v,sizeof(next.remoteUrl));if(key=="token")strlcpy(next.token,v,sizeof(next.token));
   }else if(key=="ota_min"||key=="remote_sec"){
     float n;if(!number(p.value(),n)||floorf(n)!=n||(key=="ota_min"?(n<1||n>1440):(n<30||n>3600))){reply(400,"Invalid OTA interval (1-1440 min) or remote interval (30-3600 sec)");return;}
     if(key=="ota_min")next.otaMin=n;else next.remoteSec=n;
   }else if(key=="remote_enabled"){if(!p.value().is<bool>()){reply(400,"remote_enabled must be boolean");return;}next.remoteEnabled=p.value().as<bool>();}
   else{reply(400,"Unknown system field");return;}
 }
 if(next.remoteEnabled&&(!next.remoteUrl[0]||!next.token[0])){reply(400,"Remote URL and token required before enabling");return;}
 sys=next;saveSystem();notice("SETTINGS SAVED");reply(200,"System settings saved; Check now uses the new URL immediately");
}
void webBegin(){
 const char* headers[]={"X-Shunya-Key","Content-Type"};server.collectHeaders(headers,2);
 server.on("/",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",WEB_UI);});
 server.on("/provision",HTTP_GET,[]{server.sendHeader("Location","/#wifi");server.send(302,"text/plain","");});
 server.on("/api/status",HTTP_GET,[]{JsonDocument d;statusJson(d,authorized());String s;serializeJson(d,s);server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",s);});
 onPost("/api/target",climateHandler,false);onPost("/api/config",climateHandler,false);
 onPost("/api/system",systemSettings);
 server.on("/api/events",HTTP_GET,[]{JsonDocument d;eventsJson(d);String s;serializeJson(d,s);server.send(200,"application/json",s);});
 server.on("/api/history",HTTP_GET,sendHistory);
 onPost("/api/name",[]{JsonDocument d;if(!body(d,false)||!writable())return;const char* name=d["name"].as<const char*>();if(!safeText(name,48)||!name[0]){reply(400,"Name must contain 1-48 characters");return;}strlcpy(sys.name,name,sizeof(sys.name));saveSystem();reply(200,"Name saved");},false);
 onPost("/api/wifi",[]{JsonDocument d;if(!body(d)||!writable())return;const char* ssid=d["ssid"].as<const char*>();const char* pass=d["password"].as<const char*>();if(!safeText(ssid,32)||!ssid[0]||!safeText(pass,63)||(strlen(pass)>0&&strlen(pass)<8)){reply(400,"SSID 1-32 bytes; WPA password 8-63 bytes, or empty for an open network");return;}strlcpy(sys.ssid,ssid,sizeof(sys.ssid));strlcpy(sys.password,pass,sizeof(sys.password));saveSystem();reply(200,"Wi-Fi saved; safe restart scheduled. Reconnect to setup AP if the network is unavailable.");requestRestart();});
 server.on("/api/wifi/scan",HTTP_GET,[]{int count=WiFi.scanComplete();JsonDocument d;auto a=d["networks"].to<JsonArray>();if(count>=0){for(int i=0;i<count&&i<25;++i){auto o=a.add<JsonObject>();o["ssid"]=WiFi.SSID(i);o["rssi"]=WiFi.RSSI(i);}WiFi.scanDelete();d["scanning"]=false;}else{if(count==WIFI_SCAN_FAILED)WiFi.scanNetworks(true);d["scanning"]=true;}String s;serializeJson(d,s);server.send(200,"application/json",s);});
 auto check=[](){requestOtaCheck();reply(202,otaResult);};
 server.on("/api/check-update",HTTP_GET,check);onPost("/api/check-update",check,false);
 onPost("/api/install-update",[]{JsonDocument d;if(!body(d))return;if((d["confirm"]|String(""))!="INSTALL"){reply(400,"Explicit confirmation INSTALL required");return;}requestOtaInstall();reply(otaApproved?202:409,otaResult);});
 onPost("/api/restart",[]{JsonDocument d;if(!body(d)||!writable())return;reply(202,"Restart after safe cooldown; configuration retained");requestRestart();});
 onPost("/api/reset-climate",[]{JsonDocument d;if(!body(d)||!writable())return;Config defaults;if(commitClimate(defaults))reply(200,"Climate defaults restored; Wi-Fi/system retained");});
 auto forget=[](){JsonDocument d;if(!body(d)||!writable())return;sys.ssid[0]=0;sys.password[0]=0;saveSystem();reply(202,"Wi-Fi erased; safe restart to setup AP");requestRestart();};
 onPost("/api/forget-wifi",forget);onPost("/api/wifi/forget",forget);
 onPost("/api/factory-reset",[]{JsonDocument d;if(!body(d)||!writable())return;if((d["confirm"]|String(""))!="RESET"){reply(400,"Type RESET to confirm");return;}
   Preferences p;for(const char* name:{"uj-climate","uj-system","uj-auth"}){p.begin(name,false);p.clear();p.end();}
   // Keep thermal safety latch and remote anti-replay revision: neither is a user
   // configuration. Climate defaults and fresh local access code apply at reboot.
   reply(202,"User settings erased; restart into first-use setup after cooldown");requestRestart();});
 onPost("*",[]{if(server.uri().startsWith("/api/device/")){deviceHandler();return;}reply(404,"Unknown API endpoint");},false);
 server.onNotFound([](){
   if(server.uri().startsWith("/api/device/")&&server.method()==HTTP_POST){deviceHandler();return;}
   if(server.uri().startsWith("/api/")){reply(404,"Unknown API endpoint");return;}
   // Android/Apple/Windows captive probes all reach the local setup page.
   if(apActive){server.sendHeader("Location","http://192.168.4.1/#wifi");server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");
 });
 server.begin();
}
