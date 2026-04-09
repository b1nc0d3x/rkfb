#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#define CRU_PA    0xff760000UL
#define VIOGRF_PA 0xff770000UL
#define VOP_PA    0xff900000UL
int main(void){
 int fd=open("/dev/mem",O_RDWR); if(fd<0) err(1,"open /dev/mem");
 volatile uint32_t *cru=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA);
 volatile uint32_t *vio=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIOGRF_PA);
 volatile uint32_t *vop=mmap(NULL,0x2000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
 if(cru==MAP_FAILED||vio==MAP_FAILED||vop==MAP_FAILED) err(1,"mmap");
 printf("before: clksel49=0x%08x soc20=0x%08x sys=0x%08x scan=0x%08x\n", cru[0x00c4/4], vio[0x250/4], vop[0x8/4], vop[0x2a0/4]);
 cru[0x00c4/4]=0x00002608;
 vop[0x0010/4]=0x00000000;
 vop[0x0030/4]=0x00000011;
 vop[0x003c/4]=0x05000500;
 vop[0x0048/4]=0x02cf04ff;
 vop[0x004c/4]=0x02cf04ff;
 vop[0x0050/4]=0x00000000;
 vop[0x0008/4]=0x20821800;
 vop[0x0000/4]=0x00000001;
 usleep(50000);
 printf("after:  clksel49=0x%08x soc20=0x%08x sys=0x%08x scan=0x%08x intr=0x%08x\n", cru[0x00c4/4], vio[0x250/4], vop[0x8/4], vop[0x2a0/4], vop[0x284/4]);
 for(int i=0;i<10;i++){ printf("[%d] scan=0x%08x sys=0x%08x\n", i, vop[0x2a0/4], vop[0x8/4]); usleep(20000);} 
 return 0;
}
