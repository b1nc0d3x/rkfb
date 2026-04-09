#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <err.h>
#define CRU_PA 0xff760000UL
#define VIO_PA 0xff770000UL
#define EAST_PA 0xff8f0000UL
static volatile uint32_t *cru,*vio,*vop;
static inline void dsb(void){ __asm volatile("dsb sy" ::: "memory"); }
static inline uint32_t VR(uint32_t o){ return vop[o/4]; }
static inline void VW(uint32_t o,uint32_t v){ vop[o/4]=v; dsb(); }
static inline void CW(uint32_t o,uint32_t v){ cru[o/4]=v; dsb(); }
static inline void CHW(uint32_t o,uint32_t m,uint32_t v){ CW(o,(m<<16)|(v&m)); }
int main(void){int fd=open("/dev/mem",O_RDWR); if(fd<0) err(1,"open"); cru=mmap(NULL,0x2000,3,1,fd,CRU_PA); vio=mmap(NULL,0x1000,3,1,fd,VIO_PA); vop=mmap(NULL,0x2000,3,1,fd,EAST_PA); if(cru==MAP_FAILED||vio==MAP_FAILED||vop==MAP_FAILED) err(1,"mmap");
/* route HDMI to east block */
vio[0x250/4]=(0x00ffu<<16)|0x0040u; dsb();
/* ungate east clocks */
CHW(0x0228,(1u<<10)|(1u<<11)|(1u<<13),0);
CHW(0x0270,(1u<<6)|(1u<<7),0);
CHW(0x00c8,(3u<<8)|0xffu,(1u<<8)|7u);
/* simple timing */
uint32_t sc=VR(0x0008); sc &= ~(1u<<11); sc |= (1u<<1); VW(0x0008,sc);
VW(0x0188,0x06710027); VW(0x018c,0x01040604); VW(0x0190,0x02ed0004); VW(0x0194,0x001902e9);
VW(0x003c,0x05000500); VW(0x0048,(719u<<16)|1279u); VW(0x004c,(719u<<16)|1279u); VW(0x0050,(25u<<16)|260u); VW(0x0030,0x00000011); VW(0x0000,0x01);
printf("east sys=%08x scan=%08x route=%08x clksel50=%08x\n",VR(0x0008),VR(0x02a0),vio[0x250/4],cru[0x00c8/4]);
for(int i=0;i<10;i++){usleep(10000); printf("[%d] scan=%08x intr=%08x\n",i,VR(0x02a0),VR(0x0284));}
return 0; }
