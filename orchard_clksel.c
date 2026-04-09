#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <err.h>
#define CRU_PA 0xff760000UL
int main(void){int fd=open("/dev/mem",O_RDWR); volatile uint32_t *c; if(fd<0) err(1,"open"); c=mmap(NULL,0x2000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA); if(c==MAP_FAILED) err(1,"mmap");
printf("CLKSEL42=0x%08x\n", c[0x00a8/4]);
printf("CLKSEL43=0x%08x\n", c[0x00ac/4]);
printf("CLKSEL45=0x%08x\n", c[0x00b4/4]);
printf("CLKSEL47=0x%08x\n", c[0x00bc/4]);
printf("CLKSEL48=0x%08x\n", c[0x00c0/4]);
printf("CLKSEL49=0x%08x\n", c[0x00c4/4]);
printf("CLKSEL50=0x%08x\n", c[0x00c8/4]); return 0;}
