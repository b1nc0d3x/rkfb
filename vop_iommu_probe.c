#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VOP_BIG_PA  0xff900000UL
#define IOMMU_BIG_PA 0xff903f00UL
#define IOMMU_LIT_PA 0xff8f3f00UL

static inline uint32_t R(volatile uint32_t *b, uint32_t o){ return b[o/4]; }
int main(void){
    int fd=open("/dev/mem", O_RDONLY); if(fd<0) err(1,"open");
    volatile uint32_t *vop=mmap(NULL,0x2000,PROT_READ,MAP_SHARED,fd,VOP_BIG_PA);
    volatile uint32_t *ib=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,IOMMU_BIG_PA);
    volatile uint32_t *il=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,IOMMU_LIT_PA);
    if(vop==MAP_FAILED||ib==MAP_FAILED||il==MAP_FAILED) err(1,"mmap");
    printf("VOP_BIG WIN0_MST=%08x SYS=%08x\n", R(vop,0x0040), R(vop,0x0008));
    printf("IOMMU_BIG: DTE_ADDR=%08x STATUS=%08x CMD=%08x INT_RAW=%08x INT_MASK=%08x AUTO_GATING=%08x\n",
        R(ib,0x00),R(ib,0x04),R(ib,0x08),R(ib,0x10),R(ib,0x14),R(ib,0x24));
    printf("IOMMU_LIT: DTE_ADDR=%08x STATUS=%08x CMD=%08x INT_RAW=%08x INT_MASK=%08x AUTO_GATING=%08x\n",
        R(il,0x00),R(il,0x04),R(il,0x08),R(il,0x10),R(il,0x14),R(il,0x24));
    return 0;
}
