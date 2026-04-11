#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>
#define VOP_BIG_PA 0xff900000UL
static inline uint32_t R(volatile uint32_t *b, uint32_t o){ return b[o/4]; }
int main(void){
 int fd=open("/dev/mem",O_RDONLY); if(fd<0) err(1,"open");
 volatile uint32_t *v=mmap(NULL,0x3000,PROT_READ,MAP_SHARED,fd,VOP_BIG_PA);
 if(v==MAP_FAILED) err(1,"mmap");
 printf("WIN0_CTRL0=%08x CTRL1=%08x CTRL2=%08x\n",R(v,0x0030),R(v,0x0034),R(v,0x006c));
 printf("WIN0_MST=%08x CBR=%08x ACT=%08x DSP=%08x ST=%08x VIR=%08x\n",R(v,0x0040),R(v,0x0044),R(v,0x0048),R(v,0x004c),R(v,0x0050),R(v,0x003c));
 printf("AFBCD0_CTRL=%08x HDR_PTR=%08x PIC_SIZE=%08x STATUS=%08x\n",R(v,0x0200),R(v,0x0204),R(v,0x0208),R(v,0x020c));
 printf("AFBCD1_CTRL=%08x HDR_PTR=%08x PIC_SIZE=%08x STATUS=%08x\n",R(v,0x0220),R(v,0x0224),R(v,0x0228),R(v,0x022c));
 return 0; }
