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
 printf("before: soc_con20=0x%08x sys_ctrl=0x%08x\n", viogrf[0x250/4], vop[0x8/4]);
 viogrf[0x250/4]=(1u<<22)|(1u<<6);
 uint32_t sc=vop[0x8/4]; sc &= ~(1u<<11); sc |= (1u<<1); vop[0x8/4]=sc; vop[0x0/4]=1;
 usleep(50000);
 printf("after:  soc_con20=0x%08x sys_ctrl=0x%08x\n", viogrf[0x250/4], vop[0x8/4]);
 return 0;
}
