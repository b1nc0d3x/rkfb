#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <err.h>

#define CRU_PA 0xff760000UL
static volatile uint32_t *cru;
static inline uint32_t R(uint32_t o){ return cru[o/4]; }
static inline void W(uint32_t o, uint32_t v){ cru[o/4]=v; __asm volatile("dsb sy" ::: "memory"); }
static inline void HW(uint32_t o, uint32_t mask, uint32_t val){ W(o, (mask<<16) | (val & mask)); }
static void show(const char *n, uint32_t reg, int bit){ uint32_t v = R(reg); printf("%-18s reg=0x%04x bit=%2d raw=0x%08x gated=%d\n", n, reg, bit, v, (v>>bit)&1); }
int main(void){
 int fd = open("/dev/mem", O_RDWR); if(fd<0) err(1,"open"); cru = mmap(NULL,0x2000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA); if(cru==MAP_FAILED) err(1,"mmap");
 printf("BSD orchard clock view\n");
 show("fabric_a", 0x022c, 0); /* clkgate11 bit0 */
 show("fabric_p", 0x022c, 1);
 show("guard_a",  0x022c, 12);
 show("guard_h",  0x022c, 3);
 show("guard_p",  0x022c, 10);
 show("hdmi_ctl", 0x0274, 6);
 show("sfr",      0x022c, 6);
 show("cec",      0x022c, 7);
 show("vio_noc",  0x0274, 0);
 show("vio_grf",  0x0274, 12);
 show("west_a_pre",0x0228, 8);
 show("west_h_pre",0x0228, 9);
 show("west_px",   0x0228, 12);
 show("west_a",    0x0270, 3);
 show("west_h",    0x0270, 2);
 show("east_a_pre",0x0228,10);
 show("east_h_pre",0x0228,11);
 show("east_px",   0x0228,13);
 show("east_a",    0x0270, 7);
 show("east_h",    0x0270, 6);

 printf("\nUngating full display orchard path...\n");
 HW(0x022c, (1u<<0)|(1u<<1)|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<12), 0);
 HW(0x0228, (1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<13), 0);
 HW(0x0270, (1u<<2)|(1u<<3)|(1u<<6)|(1u<<7), 0);
 HW(0x0274, (1u<<0)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<12), 0);

 printf("after:\n");
 show("fabric_a", 0x022c, 0);
 show("fabric_p", 0x022c, 1);
 show("guard_a",  0x022c, 12);
 show("guard_h",  0x022c, 3);
 show("guard_p",  0x022c, 10);
 show("hdmi_ctl", 0x0274, 6);
 show("sfr",      0x022c, 6);
 show("cec",      0x022c, 7);
 show("vio_noc",  0x0274, 0);
 show("vio_grf",  0x0274, 12);
 show("west_a_pre",0x0228, 8);
 show("west_h_pre",0x0228, 9);
 show("west_px",   0x0228, 12);
 show("west_a",    0x0270, 3);
 show("west_h",    0x0270, 2);
 show("east_a_pre",0x0228,10);
 show("east_h_pre",0x0228,11);
 show("east_px",   0x0228,13);
 show("east_a",    0x0270, 7);
 show("east_h",    0x0270, 6);
 return 0; }
