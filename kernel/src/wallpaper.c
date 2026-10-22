#include "wallpaper.h"
static uint16_t u16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int wallpaper_decode(wallpaper_t *out,const uint8_t *data,uint64_t size){
    if(!out)return 0;
    out->pixels=0;
    if(!data||size<54||data[0]!='B'||data[1]!='M')return 0;
    uint32_t header=u32(data+14),offset=u32(data+10),width=u32(data+18);
    int32_t signed_height=(int32_t)u32(data+22);
    if(header<40||header>size-14||offset<(uint64_t)14+header||offset>size||
       !width||width>4096||signed_height==0||signed_height==INT32_MIN||
       u16(data+26)!=1||u16(data+28)!=24||u32(data+30)!=0)return 0;
    uint32_t height=signed_height<0?(uint32_t)-signed_height:(uint32_t)signed_height;
    if(height>4096)return 0;
    uint32_t stride=(width*3+3)&~3U;
    if((uint64_t)stride*height>size-offset)return 0;
    *out=(wallpaper_t){data+offset,width,height,stride,signed_height<0};return 1;
}
