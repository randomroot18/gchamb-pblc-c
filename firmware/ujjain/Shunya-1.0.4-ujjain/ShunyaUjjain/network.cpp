#include "runtime.h"
#include "tls_roots.h"
#include <mbedtls/sha256.h>
#include <esp_partition.h>
#include <ctype.h>

// An embedded identity marker is checked against the manifest during download.
// This prevents a correct hash for a mismatched family/version being committed.
extern const char IMAGE_IDENTITY[] __attribute__((used)) = "SHUNYA_IMAGE:" FW_DEVICE_FAMILY ":" FW_VERSION ":END";
static uint64_t remoteThrough=0;
static uint32_t nextCheckAt=0,lastRemote=0;static bool connectedBefore=false,installJobSent=false;
static bool versionTuple(const char* s,unsigned v[3]){
 for(int i=0;i<3;++i){if(!isdigit((unsigned char)*s))return false;uint32_t val=0;while(isdigit((unsigned char)*s)){val=val*10+(*s++-'0');if(val>65535)return false;}v[i]=val;if(i<2&&*s++!='.')return false;}
 if(*s=='-'){++s;if(!*s)return false;while(*s){if(!isalnum((unsigned char)*s)&&*s!='-'&&*s!='.')return false;++s;}}
 return *s==0;
}
static bool newer(const char* offered){unsigned a[3],b[3];if(!versionTuple(offered,a)||!versionTuple(FW_VERSION,b))return false;for(int i=0;i<3;++i)if(a[i]!=b[i])return a[i]>b[i];return false;}
static bool validHttps(const char* url){return strncmp(url,"https://",8)==0&&strlen(url)>8&&!strchr(url,'\r')&&!strchr(url,'\n')&&!strchr(url,'@');}
static bool validSha(const char* sha){if(strlen(sha)!=64)return false;for(int i=0;i<64;++i)if(!isxdigit((unsigned char)sha[i]))return false;return true;}
static bool beginHttp(HTTPClient& h,WiFiClientSecure& tls,const char* url){
 if(!validHttps(url))return false;tls.setCACert(ROOT_CA_BUNDLE);tls.setHandshakeTimeout(10);tls.setTimeout(HTTP_TIMEOUT_MS);h.setConnectTimeout(HTTP_TIMEOUT_MS);h.setTimeout(HTTP_TIMEOUT_MS);
 // No redirects: pin the exact HTTPS URL and never follow an HTTP downgrade.
 // raw.githubusercontent.com / direct assets work without redirects.
 h.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);return h.begin(tls,url);
}
static bool boundedBody(HTTPClient& h,String& body,size_t limit){
 // Requiring Content-Length makes both size and memory bounds unambiguous.
 int len=h.getSize();if(len<0||(size_t)len>limit)return false;body="";body.reserve(len+1);auto* stream=h.getStreamPtr();uint32_t start=millis(),last=start;
 while(body.length()<(size_t)len){int avail=stream->available();if(avail){char b[256];size_t want=std::min<size_t>(sizeof(b),std::min<size_t>(avail,len-body.length()));size_t n=stream->readBytes(b,want);if(n){body.concat(b,n);last=millis();}}
 else {if(!stream->connected()||elapsed(millis(),last)>HTTP_TIMEOUT_MS||elapsed(millis(),start)>30000)return false;vTaskDelay(pdMS_TO_TICKS(10));}}
 return true;
}
static void fetchManifest(const NetJob& job,NetResult& r){
 WiFiClientSecure tls;HTTPClient h;if(!beginHttp(h,tls,job.sys.otaUrl)){strlcpy(r.message,"Manifest URL invalid",sizeof(r.message));return;}
 int code=h.GET();if(code!=200){char tlsErr[72]="";if(code<0)tls.lastError(tlsErr,sizeof(tlsErr));snprintf(r.message,sizeof(r.message),"Manifest error %d %s | TLS %s | heap %u max %u",code,h.errorToString(code).c_str(),tlsErr,(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getMaxAllocHeap());h.end();return;}
 String body;if(!boundedBody(h,body,4096)){h.end();strlcpy(r.message,"Manifest missing length, oversized or interrupted",sizeof(r.message));return;}h.end();
 JsonDocument d;auto err=deserializeJson(d,body);const char* version=d["version"]|"";const char* url=d["url"]|"";const char* sha=d["sha256"]|"";const char* family=d["device_family"]|"";unsigned tuple[3];
 if(err||strcmp(family,FW_DEVICE_FAMILY)||!validHttps(url)||!validSha(sha)||strlen(version)>=sizeof(r.version)||strlen(url)>=sizeof(r.url)||!versionTuple(version,tuple)){
   strlcpy(r.message,"Manifest invalid: family/version/HTTPS/hash",sizeof(r.message));return;}
 r.ok=true;r.available=newer(version);strlcpy(r.version,version,sizeof(r.version));strlcpy(r.url,url,sizeof(r.url));strlcpy(r.sha,sha,sizeof(r.sha));for(char* p=r.sha;*p;++p)*p=tolower(*p);
 snprintf(r.message,sizeof(r.message),r.available?"Update %s available; approval required":"No newer update (offered %s)",version);
}
static void installImage(const NetJob& job,NetResult& r){
 if(!newer(job.version)||!validSha(job.sha)){strlcpy(r.message,"Install rejected: stale version or hash",sizeof(r.message));return;}
 const auto* target=esp_ota_get_next_update_partition(nullptr);const auto* run=esp_ota_get_running_partition();
 if(!target||!run||target->address==run->address){strlcpy(r.message,"No inactive OTA slot",sizeof(r.message));return;}
 WiFiClientSecure tls;HTTPClient h;if(!beginHttp(h,tls,job.url)){strlcpy(r.message,"Firmware URL invalid",sizeof(r.message));return;}
 int code=h.GET(),length=h.getSize();if(code!=200||length<=0||(size_t)length>target->size){char tlsErr[64]="";if(code<0)tls.lastError(tlsErr,sizeof(tlsErr));snprintf(r.message,sizeof(r.message),"Image rejected: HTTP %d, length %d, slot %lu %s",code,length,(unsigned long)target->size,tlsErr);h.end();return;}
 if(!Update.begin(length)){snprintf(r.message,sizeof(r.message),"Update.begin: %s",Update.errorString());h.end();return;}
 mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);bool ok=mbedtls_sha256_starts(&sha,0)==0;
 uint8_t buf[2048];size_t total=0;uint32_t last=millis(),start=last;
 char marker[128];snprintf(marker,sizeof(marker),"SHUNYA_IMAGE:%s:%s:END",FW_DEVICE_FAMILY,job.version);size_t match=0;bool found=false;
 auto* stream=h.getStreamPtr();
 while(ok&&total<(size_t)length){
   Snapshot s=snapshot();if(!s.c.ota||!s.c.safeToReboot(s.now)){strlcpy(r.message,"OTA safety hold lost",sizeof(r.message));ok=false;break;}
   if(elapsed(millis(),start)>DOWNLOAD_TOTAL_MS){strlcpy(r.message,"OTA total timeout",sizeof(r.message));ok=false;break;}
   int avail=stream->available();if(avail){size_t want=std::min<size_t>(sizeof(buf),std::min<size_t>(avail,length-total));size_t n=stream->readBytes(buf,want);
     if(n){
       for(size_t i=0;i<n;++i){if(buf[i]==(uint8_t)marker[match]){++match;if(marker[match]==0){found=true;match=0;}}else match=buf[i]==(uint8_t)marker[0]?1:0;}
       if(Update.write(buf,n)!=n||mbedtls_sha256_update(&sha,buf,n)!=0){strlcpy(r.message,"Image flash/hash write failed",sizeof(r.message));ok=false;break;}
       total+=n;last=millis();
     }
   }else if(!stream->connected()||elapsed(millis(),last)>DOWNLOAD_STALL_MS){strlcpy(r.message,"Image interrupted / stalled",sizeof(r.message));ok=false;break;}
   vTaskDelay(pdMS_TO_TICKS(1));
 }
 h.end();unsigned char digest[32];if(mbedtls_sha256_finish(&sha,digest)!=0)ok=false;mbedtls_sha256_free(&sha);
 char hex[65];for(int i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
 if(!ok||total!=(size_t)length||strcmp(hex,job.sha)||!found){Update.abort();if(!r.message[0])strlcpy(r.message,!found?"Image identity mismatch":"SHA-256 mismatch / incomplete image",sizeof(r.message));return;}
 // end(false) only succeeds for the full image; it performs ESP image validation
 // and selects the inactive slot. Until here the boot partition stays untouched.
 if(!Update.end(false)||!Update.isFinished()){snprintf(r.message,sizeof(r.message),"Image commit failed: %s",Update.errorString());Update.abort();return;}
 r.ok=true;snprintf(r.message,sizeof(r.message),"Installed %s; rebooting",job.version);
}
static void remoteExchange(const NetJob& job,NetResult& r){
 if(!job.sys.remoteEnabled||!job.sys.token[0]||!job.sys.remoteUrl[0])return;
 JsonDocument telemetry;
 // statusJson touches loop-owned strings; remote payload is instead built from
 // the thread-safe climate snapshot and immutable identity only.
 Snapshot s=snapshot();telemetry["device_id"]=deviceId;telemetry["version"]=FW_VERSION;telemetry["uptime_ms"]=s.uptime;telemetry["state"]=stateName(s.c.state);telemetry["top"]=s.c.top.usable(s.now)?s.c.top.t:NAN;telemetry["top_rh"]=s.c.top.usable(s.now)?s.c.top.rh:NAN;telemetry["bottom"]=s.c.bottom.usable(s.now)?s.c.bottom.t:NAN;telemetry["bottom_rh"]=s.c.bottom.usable(s.now)?s.c.bottom.rh:NAN;telemetry["flags"]=s.flags;configJson(telemetry["config"].to<JsonObject>(),s.c.cfg);
 telemetry["epoch"]=time(nullptr);auto outputs=telemetry["outputs"].to<JsonObject>();outputs["heater"]=s.c.out.heat;outputs["humidifier"]=s.c.out.hum;outputs["mist"]=s.c.out.mist;outputs["exhaust"]=s.c.out.exhaust;telemetry["heat_pending"]=s.c.heatPending;telemetry["mist_pending"]=s.c.mistPending;telemetry["water_locked"]=s.c.waterLocked(s.now);
 telemetry["last_revision"]=job.revision;JsonDocument logs;if(!deserializeJson(logs,job.logs))telemetry["samples"]=logs.as<JsonArray>();
 String body;serializeJson(telemetry,body);String base=job.sys.remoteUrl;while(base.endsWith("/"))base.remove(base.length()-1);
 WiFiClientSecure tls;HTTPClient h;String url=base+"/telemetry";
 if(!beginHttp(h,tls,url.c_str())){strlcpy(r.message,"Remote URL invalid",sizeof(r.message));return;}
 h.addHeader("Authorization",String("Bearer ")+job.sys.token);h.addHeader("Content-Type","application/json");int code=h.POST(body);h.end();
 if(code<200||code>=300){snprintf(r.message,sizeof(r.message),"Remote telemetry HTTP %d",code);return;}r.logsThrough=job.logsThrough;
 WiFiClientSecure tls2;HTTPClient h2;url=base+"/commands?device_id="+deviceId;
 if(!beginHttp(h2,tls2,url.c_str()))return;h2.addHeader("Authorization",String("Bearer ")+job.sys.token);code=h2.GET();
 if(code==204){h2.end();r.ok=true;strlcpy(r.message,"Remote telemetry delivered",sizeof(r.message));return;}
 String command;if(code==200&&boundedBody(h2,command,sizeof(r.command)-1)){strlcpy(r.command,command.c_str(),sizeof(r.command));r.ok=true;strlcpy(r.message,"Remote telemetry / command received",sizeof(r.message));}else snprintf(r.message,sizeof(r.message),"Remote command HTTP %d / invalid length",code);h2.end();
}
String ntfyResult="Not sent yet";
// Outbound ntfy.sh POST. Runs only in the HTTPS task; never touches outputs.
static void sendNtfy(const NetJob& job,NetResult& r){
 char url[96];snprintf(url,sizeof(url),"https://ntfy.sh/%s",NTFY_TOPIC);
 WiFiClientSecure tls;HTTPClient h;if(!beginHttp(h,tls,url)){strlcpy(r.message,"ntfy URL invalid",sizeof(r.message));return;}
 char prio[4];snprintf(prio,sizeof(prio),"%u",(unsigned)job.revision);
 h.addHeader("Content-Type","text/plain; charset=utf-8");
 h.addHeader("Title",job.revision>=4?"Ujjain chamber ALERT":"Ujjain chamber");
 h.addHeader("Priority",prio);
 int code=h.POST((uint8_t*)job.logs,strlen(job.logs));
 if(code==200){r.ok=true;strlcpy(r.message,"ntfy delivered",sizeof(r.message));}
 else{char tlsErr[64]="";if(code<0)tls.lastError(tlsErr,sizeof(tlsErr));snprintf(r.message,sizeof(r.message),"ntfy error %d %s | heap %u max %u",code,tlsErr,(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getMaxAllocHeap());}
 h.end();
}
void networkTask(void*){
 // Reference the identity in a live code path, preventing linker GC.
 Serial.printf("Image identity: %s\n",IMAGE_IDENTITY);
 for(;;){static NetJob job;if(xQueueReceive(netQueue,&job,portMAX_DELAY)!=pdTRUE)continue;NetResult r;r.type=job.type;
   if(WiFi.status()!=WL_CONNECTED)strlcpy(r.message,"STA offline; local control continues",sizeof(r.message));
   else if(time(nullptr)<1700000000)strlcpy(r.message,"Waiting for NTP before certificate validation",sizeof(r.message));
   else if(ESP.getFreeHeap()<OTA_MIN_HEAP)strlcpy(r.message,"Insufficient free heap for TLS; retry later",sizeof(r.message));
   else if(job.type==1)fetchManifest(job,r);else if(job.type==2)installImage(job,r);else if(job.type==4)sendNtfy(job,r);else remoteExchange(job,r);
   xQueueSend(netResults,&r,portMAX_DELAY);
 }
}
// Job types: 1 manifest check, 2 install, 3 remote exchange, 4 ntfy (text in logs, priority in revision).
static bool queueJob(uint8_t type,const char* text=nullptr,uint8_t priority=0){static NetJob j;memset(&j,0,sizeof(j));j.type=type;j.sys=sys;if(type==4){strlcpy(j.logs,text?text:"",sizeof(j.logs));j.revision=priority;}if(type==3){String logs=remoteSamples(remoteThrough,j.logsThrough);if(logs.length()>=sizeof(j.logs))return false;strlcpy(j.logs,logs.c_str(),sizeof(j.logs));j.revision=lastRevision;}if(type==2){strlcpy(j.version,availableUpdate.version,sizeof(j.version));strlcpy(j.url,availableUpdate.url,sizeof(j.url));strlcpy(j.sha,availableUpdate.sha,sizeof(j.sha));}if(netBusy||xQueueSend(netQueue,&j,0)!=pdTRUE)return false;netBusy=true;return true;}
void requestOtaCheck(){if(otaApproved){otaResult="Installation already approved";return;}if(!sys.otaUrl[0]){otaResult="Set HTTPS manifest URL in System";return;}if(queueJob(1)){otaResult="Checking for updates";event("OTA","Update checking");}else otaResult="Network worker busy; retry shortly";}
void requestOtaInstall(){
 if(!updateAvailable||netBusy||otaApproved){otaResult="No available update, or network worker busy";return;}
 if(!esp_ota_get_next_update_partition(nullptr)){otaResult="No inactive OTA partition";return;}
 if(sendCommand(CommandType::OTA_ON)){otaApproved=true;installJobSent=false;otaResult="Approved: outputs OFF; waiting for heater cooldown";event("OTA","Installation approved; entering OTA_SAFE");}
}
static void applyRemote(const char* text){
 JsonDocument d;if(deserializeJson(d,text)||!d["revision"].is<uint64_t>()){event("REMOTE","Invalid command/revision rejected");return;}
 uint64_t revision=d["revision"].as<uint64_t>();if(revision<=lastRevision)return;
 Config next=desired;String error;
 for(JsonPairConst p:d.as<JsonObjectConst>())if(strcmp(p.key().c_str(),"revision")&&strcmp(p.key().c_str(),"config")&&strcmp(p.key().c_str(),"action")){event("REMOTE","Unknown command field rejected");return;}
 if(!d["config"].isNull()&&!d["config"].is<JsonObject>()){event("REMOTE","Invalid config object");return;}
 if(!d["config"]["heater"].isNull()&&(d["config"]["heater"]|String(""))!="OFF"){event("REMOTE","Remote heater command permits OFF only");return;}

 if(d["config"].is<JsonObject>()&&!parseClimate(d["config"],next,error)){event("REMOTE",error.c_str());return;}
 const char* action=d["action"]|"";
 if(strcmp(action,"")&&strcmp(action,"mist")&&strcmp(action,"exhaust")&&strcmp(action,"heater_off")){event("REMOTE","Unsupported remote action rejected");return;}
 if(!strcmp(action,"heater_off"))next.heater=Mode::OFF;
 if(!strcmp(action,"mist"))next.mist=Mode::AUTO;
 if(!strcmp(action,"exhaust"))next.exhaust=Mode::AUTO;
 if(otaApproved){event("REMOTE","Command deferred during OTA");return;}
 if(uxQueueSpacesAvailable(commandQueue)<2){event("REMOTE","Command queue busy; retry later");return;}
 // At-most-once: persist revision before enqueuing actions. Power loss in this
 // small interval may skip an action; backend must issue a NEW revision to retry.
 Preferences p;if(!p.begin("uj-safety",false)||p.putULong64("revision",revision)!=sizeof(uint64_t)){p.end();event("REMOTE","Revision persistence failed; command rejected");return;}p.end();lastRevision=revision;
 desired=next;saveClimate();sendCommand(CommandType::CONFIG,&desired);
 if(!strcmp(action,"mist"))sendCommand(CommandType::MIST);if(!strcmp(action,"exhaust"))sendCommand(CommandType::EXHAUST);
 event("REMOTE","New revision applied through local safety controller");
}
void networkService(){
 uint32_t n=millis();bool connected=WiFi.status()==WL_CONNECTED;
 if(connected&&!connectedBefore){nextCheckAt=n+FIRST_OTA_MS;if(!nextCheckAt)nextCheckAt=1;}connectedBefore=connected;
 NetResult r;while(xQueueReceive(netResults,&r,0)==pdTRUE){netBusy=false;
   if(r.type==1){otaResult=r.message;if(r.ok){availableUpdate=r;updateAvailable=r.available;}event("OTA",r.message);}
   if(r.type==2){otaResult=r.message;event("OTA",r.message);if(r.ok){delay(100);ESP.restart();}else {if(sendCommand(CommandType::OTA_OFF)){otaApproved=false;installJobSent=false;}else otaResult="Install failed; safe hold retained - restart device";}}
   if(r.type==3){if(r.logsThrough>remoteThrough)remoteThrough=r.logsThrough;event("REMOTE",r.message);if(r.ok&&r.command[0])applyRemote(r.command);}
   if(r.type==4){static bool lastOk=true;ntfyResult=r.message;if(!r.ok||!lastOk)event("NTFY",r.message);lastOk=r.ok;}
 }
 if(otaApproved&&!installJobSent&&!netBusy){Snapshot s=snapshot();if(s.c.ota&&s.c.safeToReboot(s.now)){if(queueJob(2)){installJobSent=true;otaResult="Downloading and verifying approved update";}}}
 if(!otaApproved&&!netBusy&&connected&&sys.otaUrl[0]&&nextCheckAt&&(int32_t)(n-nextCheckAt)>=0){nextCheckAt=n+sys.otaMin*60000;if(!nextCheckAt)nextCheckAt=1;requestOtaCheck();}
 if(!otaApproved&&!netBusy&&connected&&sys.remoteEnabled&&sys.remoteUrl[0]&&sys.token[0]&&elapsed(n,lastRemote)>=sys.remoteSec*1000){lastRemote=n;queueJob(3);}
 // ntfy: routine status every NTFY_PERIOD_MS, immediate alert on a new fault
 // (rate limited), and one message when the fault clears. Skipped during OTA.
 if(NTFY_TOPIC[0]&&!otaApproved&&!netBusy&&connected&&time(nullptr)>1700000000){
   static uint32_t lastNtfy=0,lastAlert=0;static bool sentOnce=false,lastFault=false;
   Snapshot s=snapshot();const Controller& c=s.c;uint32_t sn=s.now;
   bool fault=c.hot||(s.uptime>60000&&(!c.top.healthy||!c.bottom.healthy));
   bool due=!sentOnce||elapsed(n,lastNtfy)>=NTFY_PERIOD_MS;
   bool alert=fault&&!lastFault&&(!lastAlert||elapsed(n,lastAlert)>=NTFY_ALERT_GAP_MS);
   bool cleared=!fault&&lastFault;
   if(s.controlReady&&(due||alert||cleared)){
     char top[32],bot[32],text[420];
     if(c.top.usable(sn))snprintf(top,sizeof(top),"%.1fC %.0f%%",c.top.t,c.top.rh);else strlcpy(top,"OFFLINE",sizeof(top));
     if(c.bottom.usable(sn))snprintf(bot,sizeof(bot),"%.1fC %.0f%%",c.bottom.t,c.bottom.rh);else strlcpy(bot,"OFFLINE",sizeof(bot));
     snprintf(text,sizeof(text),"%s %s | %s | top %s | bottom %s | avg %.1fC | target %.1fC %.0f%% | heat %d hum %d mist %d exh %d | %s%s | rssi %d heap %u",
       fault?"ALERT":(cleared?"RECOVERED":"OK"),FW_VERSION,stateName(c.state),top,bot,(double)c.average(sn),
       (double)c.cfg.temperature_target,(double)c.cfg.humidity_target,(int)c.out.heat,(int)c.out.hum,(int)c.out.mist,(int)c.out.exhaust,
       c.hot?"HIGH TEMPERATURE | ":"",c.reason?c.reason:"",(int)WiFi.RSSI(),(unsigned)ESP.getFreeHeap());
     if(queueJob(4,text,fault?4:(cleared?3:1))){lastNtfy=n;sentOnce=true;if(alert)lastAlert=n;lastFault=fault;}
   }
 }
}
