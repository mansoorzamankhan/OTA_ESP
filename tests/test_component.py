"""Compile and exercise the real BLE OTA protocol engine without hardware."""
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "ota_protocol.h"
#include <assert.h>
#include <string.h>
static unsigned begins, writes, ends, aborts;
static bool reject_begin, reject_write, reject_end;
static uint8_t persisted[8192];
static size_t saved;
static bool begin(uint32_t n) {begins++; saved=0; return n<=sizeof(persisted) && !reject_begin;}
static bool write_data(const uint8_t *p,size_t n) {
 writes++; if(reject_write) return false;
 memcpy(persisted+saved,p,n);saved+=n;return true;
}
static bool end(void) {ends++;return !reject_end;}
static void abort_data(void) {aborts++;}
static ota_protocol_t s = {.backend={begin,write_data,end,abort_data}};
static void crc(uint8_t *p,size_t n) {uint16_t c=ota_crc16(p,n);p[n]=c;p[n+1]=c>>8;}
static void cmd(unsigned id,unsigned length,uint8_t a[20]) {
 uint8_t p[20]={0};p[0]=id;
 for(unsigned i=0;i<4;i++)p[i+2]=length>>(8*i);
 crc(p,18);assert(ota_protocol_command(&s,p,20,a));
 assert(ota_crc16(a,18)==(uint16_t)(a[18]|a[19]<<8));
}
static void sector(unsigned index,const uint8_t *data,size_t n,bool corrupt,uint8_t a[20]) {
 uint8_t p[512];size_t at=0;unsigned seq=0;
 while(n-at>500) {
  p[0]=index;p[1]=index>>8;p[2]=seq++;
  memcpy(p+3,data+at,500);
  assert(!ota_protocol_data(&s,p,503,a));at+=500;
 }
 p[0]=index;p[1]=index>>8;p[2]=255;
 memcpy(p+3,data+at,n-at);
 uint16_t c=ota_crc16(data,n);p[3+n-at]=c;p[4+n-at]=(c>>8)^corrupt;
 assert(ota_protocol_data(&s,p,n-at+5,a));
}
int main(void) {
 uint8_t a[20], image[5000], progress[10];
 for(unsigned i=0;i<sizeof(image);i++)image[i]=(i*17)^0x5a;
 assert(ota_crc16((const uint8_t*)"123456789",9)==0x31c3);
 uint8_t bad[20]={1};assert(!ota_protocol_command(&s,bad,20,a));
 assert(!ota_protocol_command(&s,bad,1,a));
 cmd(1,0,a);assert(a[4]==1 && !s.active);
 reject_begin=true;cmd(1,5000,a);assert(a[4]==1);reject_begin=false;
 cmd(1,5000,a);assert(a[4]==0 && s.active);
 sector(0,image,4096,true,a);assert(a[2]==1 && !writes);
 sector(0,image,4096,false,a);assert(a[2]==0 && writes==1);
 ota_protocol_progress(&s,progress);assert(progress[0]==0 && progress[1]==16);
 uint8_t wrong[5]={0,0,255,0,0};
 assert(ota_protocol_data(&s,wrong,5,a) && a[2]==2 && a[4]==1 && writes==1);
 sector(0xffff,image+4096,904,false,a);assert(a[2]==0 && s.complete && ends==1);
 assert(saved==sizeof(image) && !memcmp(image,persisted,sizeof(image)));
 cmd(2,0,a);assert(s.complete && a[4]==0); // stop cannot undo boot selection
 ota_protocol_reset(&s);
 cmd(1,5000,a);
 uint8_t packet[10]={0,0,1}; // missing packet zero
 assert(ota_protocol_data(&s,packet,sizeof(packet),a) && a[2]==2);
 packet[2]=255;
 assert(ota_protocol_data(&s,packet,sizeof(packet),a) && a[2]==2);
 sector(0,image,4096,false,a);assert(a[2]==0);
 unsigned old=aborts;
 ota_protocol_reset(&s);assert(aborts==old+1 && !s.active);
 cmd(1,40,a);sector(0,image,39,false,a);assert(a[2]==3);
 sector(0,image,40,true,a);assert(a[2]==1);
 sector(0,image,40,false,a);assert(a[2]==0 && s.complete);
 ota_protocol_reset(&s);
 cmd(1,40,a);reject_write=true;old=aborts;
 sector(0,image,40,false,a);assert(a[2]!=0 && !s.active && aborts==old+1);
 reject_write=false;cmd(1,40,a);reject_end=true;
 sector(0,image,40,false,a);assert(a[2]!=0 && !s.complete && !s.active);
 reject_end=false;cmd(1,40,a);cmd(2,0,a);assert(!s.active && !s.total);
 assert(ota_protocol_data(&s,packet,1,a) && a[2]!=0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="otable-protocol-") as directory:
    tmp = pathlib.Path(directory)
    (tmp / "test.c").write_text(SOURCE)
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-g",
        f"-I{ROOT / 'components/OtaBle'}",
        str(ROOT / "components/OtaBle/ota_protocol.c"), str(tmp / "test.c"),
        "-o", str(tmp / "test"),
    ], check=True)
    subprocess.run([str(tmp / "test")], check=True,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
print("BLE OTA protocol tests passed (ASan/UBSan)")
