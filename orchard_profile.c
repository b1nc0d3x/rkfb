#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <err.h>

#define CRU_PA 0xff760000UL
static volatile uint32_t *c;
static inline uint32_t R(uint32_t o){ return c[o/4]; }
static inline void W(uint32_t o, uint32_t v){ c[o/4]=v; __asm volatile("dsb sy" ::: "memory"); }
static inline void HW(uint32_t o, uint32_t mask, uint32_t val){ W(o, (mask<<16) | (val & mask)); }
int main(void){
 int fd=open("/dev/mem",O_RDWR); if(fd<0) err(1,"open"); c=mmap(NULL,0x2000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA); if(c==MAP_FAILED) err(1,"mmap");
 printf("before: 42=%08x 43=%08x 47=%08x 48=%08x 49=%08x 50=%08x\n", R(0x00a8),R(0x00ac),R(0x00bc),R(0x00c0),R(0x00c4),R(0x00c8));
 /* fabric_a/fabric_p and guard clocks from GPLL at moderate divisors */
 HW(0x00a8, (3u<<6)|0x1fu|(3u<<14)|(0x1fu<<8), (1u<<6)|3u | (1u<<14)|(3u<<8));
 HW(0x00ac, 0x1fu | (0x1fu<<5) | (0x1fu<<10), 1u | (1u<<5) | (1u<<10));
 /* west engine pre clocks: parent GPLL, moderate bus divisors */
 HW(0x00bc, (3u<<6)|0x1fu|(0x1fu<<8), (2u<<6)|1u|(3u<<8));
 /* east engine pre clocks: parent GPLL, moderate bus divisors */
 HW(0x00c0, (3u<<6)|0x1fu|(0x1fu<<8), (2u<<6)|1u|(3u<<8));
 /* west pixel: GPLL/8 ; east pixel parked low */
 HW(0x00c4, (3u<<8)|0xffu, (1u<<8)|7u);
 HW(0x00c8, (3u<<8)|0xffu, (1u<<8)|0x1fu);
 printf("after:  42=%08x 43=%08x 47=%08x 48=%08x 49=%08x 50=%08x\n", R(0x00a8),R(0x00ac),R(0x00bc),R(0x00c0),R(0x00c4),R(0x00c8));
 return 0; }
