#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>
#define VOP_PA 0xff900000UL
static volatile uint32_t *v;
static inline uint32_t R(uint32_t o){ return v[o/4]; }
static inline void W(uint32_t o, uint32_t x){ v[o/4]=x; __asm volatile("dsb sy" ::: "memory"); }
int main(void){
 int fd=open("/dev/mem",O_RDWR); if(fd<0) err(1,"open");
 v=mmap(NULL,0x3000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA); if(v==MAP_FAILED) err(1,"mmap");
 printf("before: en0=%08x clr0=%08x st0=%08x raw0=%08x st1=%08x raw1=%08x vstat=%08x line=%08x\n",R(0x0280),R(0x0284),R(0x0288),R(0x028c),R(0x0298),R(0x029c),R(0x02a4),R(0x02a0));
 W(0x0284, 0xffffffffu);
 W(0x0294, 0xffffffffu);
 usleep(10000);
 printf("cleared: st0=%08x raw0=%08x st1=%08x raw1=%08x vstat=%08x\n",R(0x0288),R(0x028c),R(0x0298),R(0x029c),R(0x02a4));
 W(0x0280, 0x0000ffffu);
 W(0x0290, 0x0000ffffu);
 for(int i=0;i<40;i++){
  usleep(10000);
  printf("[%02d] st0=%08x raw0=%08x st1=%08x raw1=%08x vstat=%08x line=%08x blank=%08x\n",i,R(0x0288),R(0x028c),R(0x0298),R(0x029c),R(0x02a4),R(0x02a0),R(0x02a8));
 }
 return 0;
}
