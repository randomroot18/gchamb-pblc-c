#include "runtime.h"
#include <LittleFS.h>
bool fsOK=false;
static Sample ram[120],batch[5];static uint32_t ramCount=0,batchCount=0;static uint64_t nextId=1,lastStored=0;
static constexpr uint32_t LOG_MAGIC=0x554A4C31;
struct DiskRow {uint32_t magic;Sample sample;uint32_t crc;};
struct Segment {char path[16];uint64_t first=UINT64_MAX,last=0;};
static Segment segments[25];
static uint32_t hashRow(const Sample& s){const uint8_t* p=(const uint8_t*)&s;uint32_t h=2166136261u;for(size_t i=0;i<sizeof(s);++i)h=(h^p[i])*16777619u;return h;}
static bool readRow(File& f,Sample& s){DiskRow r;if(f.read((uint8_t*)&r,sizeof(r))!=sizeof(r)||r.magic!=LOG_MAGIC||r.crc!=hashRow(r.sample))return false;s=r.sample;return true;}
void loggingBegin(){
 // Format only on first use; never erase a previously mounted filesystem on an error.
 Preferences p;p.begin("uj-log",false);bool initialized=p.getBool("initialized",false);
 fsOK=LittleFS.begin(!initialized);if(fsOK&&!initialized)p.putBool("initialized",true);p.end();
 if(!fsOK)return;
 for(int i=0;i<25;++i){snprintf(segments[i].path,sizeof(segments[i].path),"/hour%02d.bin",i);File f=LittleFS.open(segments[i].path,"r");if(!f)continue;
   Sample s;while(readRow(f,s)){segments[i].first=std::min(segments[i].first,s.id);segments[i].last=std::max(segments[i].last,s.id);lastStored=std::max(lastStored,s.id);}f.close();
 }
 nextId=lastStored+1;
}
static void flushBatch(){
 if(!batchCount)return;
 if(fsOK){
   for(uint32_t i=0;i<batchCount;){
     unsigned slot=((batch[i].id-1)/60)%25;uint64_t group=(batch[i].id-1)/60;Segment& seg=segments[slot];
     bool same=seg.last&&((seg.last-1)/60)==group;
     // On reboot, an incomplete tail invalidates this segment. Keep other hours.
     if(same){File check=LittleFS.open(seg.path,"r");same=check&&check.size()%sizeof(DiskRow)==0;check.close();}
     File f=LittleFS.open(seg.path,same?"a":"w");if(!f){fsOK=false;break;}
     if(!same){seg.first=UINT64_MAX;seg.last=0;}
     while(i<batchCount&&(batch[i].id-1)/60==group){DiskRow r={};r.magic=LOG_MAGIC;r.sample=batch[i];r.crc=hashRow(r.sample);
       if(f.write((const uint8_t*)&r,sizeof(r))!=sizeof(r)){fsOK=false;break;}
       seg.first=std::min(seg.first,r.sample.id);seg.last=r.sample.id;lastStored=r.sample.id;++i;
     }f.flush();f.close();if(!fsOK)break;
   }
 }
 batchCount=0;
}
void loggingService(){Sample s;while(xQueueReceive(sampleQueue,&s,0)==pdTRUE){s.id=nextId++;s.rssi=WiFi.status()==WL_CONNECTED?WiFi.RSSI():0;if(!s.rssi)s.flags|=64;ram[ramCount%120]=s;++ramCount;batch[batchCount++]=s;if(batchCount==5)flushBatch();}}
void sampleJson(JsonObject o,const Sample& s){o["id"]=s.id;o["epoch"]=s.epoch;o["uptime_ms"]=s.uptime;o["boot_id"]=s.boot;o["top"]=s.top;o["top_rh"]=s.topRH;o["bottom"]=s.bottom;o["bottom_rh"]=s.bottomRH;o["average"]=s.average;o["rh_low"]=s.rhLow;o["rh_high"]=s.rhHigh;o["state"]=stateName((State)s.state);o["output_any_bits"]=s.outputs;o["flags"]=s.flags;o["rssi"]=s.rssi;}
String recentSamplesJson(){JsonDocument d;auto a=d["samples"].to<JsonArray>();uint32_t count=std::min<uint32_t>(ramCount,5U);for(uint32_t i=0;i<count;++i)sampleJson(a.add<JsonObject>(),ram[(ramCount-count+i)%120]);String s;serializeJson(d,s);return s;}
void sendHistory(){
 bool day=server.arg("range")=="24h",csv=server.arg("format")=="csv";uint64_t minimum=nextId>1440?nextId-1440:1;
 server.sendHeader("Cache-Control","no-store");if(csv)server.sendHeader("Content-Disposition","attachment; filename=shunya-history.csv");
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,csv?"text/csv":"application/json","");
 server.sendContent(csv?"id,epoch,uptime_ms,boot_id,top,top_rh,bottom,bottom_rh,average,rh_low,rh_high,state,output_any_bits,flags,rssi\n":"{\"samples\":[");bool first=true;
 auto emit=[&](const Sample& s){
   if(csv){char line[256];snprintf(line,sizeof(line),"%llu,%lld,%llu,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%u,%lu,%d\n",(unsigned long long)s.id,(long long)s.epoch,(unsigned long long)s.uptime,(unsigned long)s.boot,s.top,s.topRH,s.bottom,s.bottomRH,s.average,s.rhLow,s.rhHigh,stateName((State)s.state),s.outputs,(unsigned long)s.flags,s.rssi);server.sendContent(line);}
   else {JsonDocument d;sampleJson(d.to<JsonObject>(),s);String line;serializeJson(d,line);if(!first)server.sendContent(",");server.sendContent(line);first=false;}
 };
 if(day&&fsOK){
   Segment sorted[25];memcpy(sorted,segments,sizeof(sorted));std::sort(sorted,sorted+25,[](const Segment&a,const Segment&b){return a.first<b.first;});
   for(auto& seg:sorted){if(seg.last<minimum)continue;File f=LittleFS.open(seg.path,"r");if(!f)continue;Sample s;while(server.client().connected()&&readRow(f,s)){if(s.id>=minimum)emit(s);delay(1);}f.close();}
 }
 uint32_t count=std::min<uint32_t>(ramCount,day?120U:60U);for(uint32_t i=0;i<count;++i){const Sample& s=ram[(ramCount-count+i)%120];if(!day||!fsOK||s.id>lastStored)emit(s);}
 if(!csv)server.sendContent("],\"aggregation\":\"1-minute means; state at end; output bits and flags OR within minute\"}");server.sendContent("");
}

String remoteSamples(uint64_t after,uint64_t& through){
 JsonDocument d;JsonArray a=d.to<JsonArray>();through=after;
 auto add=[&](const Sample& row){if(row.id>after&&a.size()<6){sampleJson(a.add<JsonObject>(),row);through=std::max(through,row.id);}};
 if(fsOK){Segment sorted[25];memcpy(sorted,segments,sizeof(sorted));std::sort(sorted,sorted+25,[](const Segment& a,const Segment& b){return a.first<b.first;});
   for(const auto& seg:sorted){if(seg.last<=after||a.size()>=6)continue;File f=LittleFS.open(seg.path,"r");if(!f)continue;Sample row;while(a.size()<6&&readRow(f,row))add(row);f.close();}
 }
 uint32_t count=std::min<uint32_t>(ramCount,120U);for(uint32_t i=0;i<count&&a.size()<6;++i){const Sample& row=ram[(ramCount-count+i)%120];if(row.id>through)add(row);}
 String text;serializeJson(d,text);return text;
}
