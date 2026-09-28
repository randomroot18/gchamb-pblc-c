#pragma once
#include "control.h"
#include <cstddef>
namespace chamber {
inline uint32_t checksum(const void* data,size_t size){uint32_t h=2166136261u;const uint8_t* b=(const uint8_t*)data;while(size--)h=(h^*b++)*16777619u;return h;}
inline bool decodeClimate(uint32_t schema,const void* data,size_t size,uint32_t crc,Config& result){
  if(!data||checksum(data,size)!=crc)return false;
  Config next;
  if(schema==1&&size==sizeof(ConfigV1)){
    ConfigV1 old;memcpy(&old,data,sizeof(old));static_cast<ConfigV1&>(next)=old;
  }else if(schema==SCHEMA&&size==sizeof(Config))memcpy(&next,data,sizeof(next));
  else return false;
  if(!validConfig(next))return false;
  result=next;return true;
}
}
