#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>
#define CRU_PA 0xff760000UL
#define VIO_PA 0xff770000UL
#define VOP_PA 0xff900000UL
static volatile uint32_t *cru,*vio,*vop;
static inline void dsb(void){ __asm volatile("dsb sy" ::: "memory"); }
static inline uint32_t R(volatile uint32_t *b,uint32_t o){ return b[o/4]; }
static inline void W(volatile uint32_t *b,uint32_t o,uint32_t v){ b[o/4]=v; dsb(); }
static inline void CHW(uint32_t o,uint32_t m,uint32_t v){ W(cru,o,(m<<16)|(v&m)); }
int main(void){
 int fd=open("/dev/mem",O_RDWR); if(fd<0) err(1,"open");
 cru=mmap(NULL,0x2000,3,1,fd,CRU_PA); vio=mmap(NULL,0x1000,3,1,fd,VIO_PA); vop=mmap(NULL,0x3000,3,1,fd,VOP_PA);
 if(cru==MAP_FAILED||vio==MAP_FAILED||vop==MAP_FAILED) err(1,"mmap");
 CHW(0x022c,(1u<<0)|(1u<<1)|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<12),0);
 CHW(0x0228,(1u<<8)|(1u<<9)|(1u<<12),0); CHW(0x0270,(1u<<2)|(1u<<3),0); CHW(0x0274,(1u<<0)|(1u<<6)|(1u<<12),0);
 CHW(0x033c,(1u<<0)|(1u<<7),0); CHW(0x0340,(1u<<2),0); CHW(0x0344,0x0fu,0);
 CHW(0x00bc,(3u<<6)|0x1fu|(0x1fu<<8),(2u<<6)|1u|(3u<<8)); CHW(0x00c4,(3u<<8)|0xffu,(2u<<8)|7u);
 W(vio,0x0250,(0x00ffu<<16)|0x0000u);
 W(vop,0x000c,0x0003a000); W(vop,0x0010,0x00000000); W(vop,0x0014,0x0000e400);
 W(vop,0x0018,0x000000ff); W(vop,0x001c,0x00711c08); W(vop,0x0020,0x00000000);
 W(vop,0x0030,0x00000000);
 W(vop,0x01a0,0x06710027); W(vop,0x01a4,0x01040604); W(vop,0x01a8,0x02ed0004); W(vop,0x01ac,0x001902e9);
 W(vop,0x01b8,0x01040604); W(vop,0x01bc,0x001902e9); W(vop,0x01cc,0x000000ff); W(vop,0x01d0,0x000000ff);
 W(vop,0x0008,0x00802002); W(vop,0x0000,0x00000001); usleep(50000);
 printf("sys=%08x dsp0=%08x dsp1=%08x vstat=%08x raw0=%08x raw1=%08x bg=%08x bg0=%08x bg1=%08x\n",R(vop,0x0008),R(vop,0x0010),R(vop,0x0014),R(vop,0x02a4),R(vop,0x028c),R(vop,0x029c),R(vop,0x0018),R(vop,0x01cc),R(vop,0x01d0));
 for(int i=0;i<20;i++){ usleep(20000); printf("[%d] vstat=%08x raw0=%08x raw1=%08x st0=%08x st1=%08x\n",i,R(vop,0x02a4),R(vop,0x028c),R(vop,0x029c),R(vop,0x0288),R(vop,0x0298)); }
 return 0; }
