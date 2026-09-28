#pragma once
#include <stdint.h>
#include <stdio.h>
// getEfuseMac() packs the first transmitted byte in bits 0..7.
inline void formatDeviceIdentity(uint64_t mac,char (&id)[20],char (&suffix)[5]){
  for(unsigned i=0;i<6;++i)snprintf(id+i*2,sizeof(id)-i*2,"%02X",(unsigned)((mac>>(8*i))&0xff));
  snprintf(suffix,sizeof(suffix),"%02X%02X",(unsigned)((mac>>32)&0xff),(unsigned)((mac>>40)&0xff));
}
