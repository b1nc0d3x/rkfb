#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>
#define CRU_PA  0xff760000UL
#define VIO_PA  0xff770000UL
#define VOP_PA  0xff900000UL
#define FB_PA   0x0f800000u
static volatile uint32_t *cru,*vio,*vop;
static inline void dsb(void){ __asm volatile("dsb sy" ::: "memory"); }
static inline uint32_t R(volatile uint32_t *b, uint32_t o){ return b[o/4]; }
static inline void W(volatile uint32_t *b, uint32_t o, uint32_t v){ b[o/4]=v; dsb(); }
static inline void CHW(uint32_t o,uint32_t m,uint32_t v){ W(cru,o,(m<<16)|(v&m)); }
int main(void){
 int fd=open("/dev/mem",O_RDWR); if(fd<0) err(1,"open");
 cru=mmap(NULL,0x2000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA);
 vio=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIO_PA);
 vop=mmap(NULL,0x3000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
 if(cru==MAP_FAILED||vio==MAP_FAILED||vop==MAP_FAILED) err(1,"mmap");
 printf("before afbc=%08x stat=%08x ctrl0=%08x ctrl2=%08x mst=%08x sys=%08x\n",R(vop,0x0200),R(vop,0x020c),R(vop,0x0030),R(vop,0x006c),R(vop,0x0040),R(vop,0x0008));
 CHW(0x0228,(1u<<12),0); CHW(0x0270,(1u<<4)|(1u<<6),0); CHW(0x0274,(1u<<0)|(1u<<4),0); CHW(0x00c4,(3u<<8)|0xffu,(1u<<8)|7u);
 W(vio,0x0250,(0x00ffu<<16)|0x0000u);
 W(vop,0x0200,0x00000000); /* disable AFBCD0 */
 W(vop,0x0204,0x00000000);
 W(vop,0x0208,0x00000000);
 W(vop,0x000c,0x0003a000);
 W(vop,0x0010,0x00000000);
 W(vop,0x0014,0x0000e400);
 W(vop,0x0018,0x00000000);
 W(vop,0x006c,0x00000000); /* ctrl2 */
 W(vop,0x003c,0x05000500);
 W(vop,0x0040,FB_PA);
 W(vop,0x0048,0x02cf04ff);
 W(vop,0x004c,0x02cf04ff);
 W(vop,0x0050,0x00190104);
 W(vop,0x0188,0x06710027);
 W(vop,0x018c,0x01040604);
 W(vop,0x0190,0x02ed0004);
 W(vop,0x0194,0x001902e9);
 W(vop,0x0030,0x00000001);
 W(vop,0x0008,0x20802002);
 W(vop,0x0000,0x00000001);
 usleep(50000);
 printf("after  afbc=%08x stat=%08x ctrl0=%08x ctrl2=%08x mst=%08x sys=%08x raw0=%08x vstat=%08x\n",R(vop,0x0200),R(vop,0x020c),R(vop,0x0030),R(vop,0x006c),R(vop,0x0040),R(vop,0x0008),R(vop,0x028c),R(vop,0x02a4));
 return 0; }
