#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#define VIOGRF_PA 0xff770000UL
#define VOP_PA    0xff900000UL
int main(void){
 int fd=open("/dev/mem",O_RDWR); if(fd<0) err(1,"open /dev/mem");
 volatile uint32_t *viogrf=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIOGRF_PA);
 volatile uint32_t *vop=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
 if(viogrf==MAP_FAILED) err(1,"mmap viogrf"); if(vop==MAP_FAILED) err(1,"mmap vop");
 for(int i=0;i<20;i++){
  printf("%2d soc_con20=0x%08x sys_ctrl=0x%08x win0=0x%08x\n", i, viogrf[0x250/4], vop[0x8/4], vop[0x40/4]);
  usleep(500000);
 }
 return 0;
}
