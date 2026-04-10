#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VOP_BIG_PA 0xff900000UL
#define VOP_LIT_PA 0xff8f0000UL

static inline uint32_t R(volatile uint32_t *b, uint32_t o){ return b[o/4]; }
static void dump(const char *name, volatile uint32_t *v)
{
    printf("== %s ==\n", name);
    printf("ver=%08x sys=%08x dsp0=%08x dsp1=%08x\n", R(v,0x0004), R(v,0x0008), R(v,0x0010), R(v,0x0014));
    printf("win0_ctrl0=%08x ctrl1=%08x vir=%08x mst=%08x\n", R(v,0x0030), R(v,0x0034), R(v,0x003c), R(v,0x0040));
    printf("act=%08x dsp=%08x st=%08x\n", R(v,0x0048), R(v,0x004c), R(v,0x0050));
    printf("htotal=%08x hact=%08x vtotal=%08x vact=%08x\n", R(v,0x0188), R(v,0x018c), R(v,0x0190), R(v,0x0194));
    printf("intr0=%08x raw0=%08x line=%08x vstat=%08x\n", R(v,0x0288), R(v,0x028c), R(v,0x02a0), R(v,0x02a4));
}
int main(void)
{
    int fd=open("/dev/mem", O_RDONLY); if(fd<0) err(1,"open");
    volatile uint32_t *big=mmap(NULL,0x2000,PROT_READ,MAP_SHARED,fd,VOP_BIG_PA);
    volatile uint32_t *lit=mmap(NULL,0x2000,PROT_READ,MAP_SHARED,fd,VOP_LIT_PA);
    if(big==MAP_FAILED||lit==MAP_FAILED) err(1,"mmap");
    dump("VOP_BIG", big);
    dump("VOP_LIT", lit);
    return 0;
}
