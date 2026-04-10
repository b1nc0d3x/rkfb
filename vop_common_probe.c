#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA  0xff760000UL
#define VIO_PA  0xff770000UL
#define VOP_PA  0xff900000UL

static volatile uint32_t *cru, *vio, *vop;
static inline void dsb(void){ __asm volatile("dsb sy" ::: "memory"); }
static inline uint32_t R(volatile uint32_t *b, uint32_t o){ return b[o/4]; }
static inline void W(volatile uint32_t *b, uint32_t o, uint32_t v){ b[o/4]=v; dsb(); }
static inline void CHW(uint32_t o, uint32_t m, uint32_t v){ W(cru,o,(m<<16)|(v&m)); }

static void common_prep(void)
{
    CHW(0x022c, (1u<<0)|(1u<<1)|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<12), 0);
    CHW(0x0228, (1u<<8)|(1u<<9)|(1u<<12), 0);
    CHW(0x0270, (1u<<2)|(1u<<3), 0);
    CHW(0x0274, (1u<<0)|(1u<<6)|(1u<<12), 0);
    CHW(0x033c, (1u<<0)|(1u<<7), 0);
    CHW(0x0340, (1u<<2), 0);
    CHW(0x0344, 0x0fu, 0);
    CHW(0x00bc, (3u<<6)|0x1fu|(0x1fu<<8), (2u<<6)|1u|(3u<<8));
    CHW(0x00c4, (3u<<8)|0xffu, (2u<<8)|7u);
    W(vio, 0x0250, (0x00ffu<<16) | 0x0000u);
}

static void mode_program(uint32_t sys, uint32_t sys1, uint32_t dsp0, uint32_t dsp1)
{
    W(vop, 0x0004, dsp0);
    W(vop, 0x000c, sys1);
    W(vop, 0x0010, 0x00000000);
    W(vop, 0x0014, dsp1);
    W(vop, 0x0188, 0x06710027);
    W(vop, 0x018c, 0x01040604);
    W(vop, 0x0190, 0x02ed0004);
    W(vop, 0x0194, 0x001902e9);
    W(vop, 0x01b8, 0x01040604);
    W(vop, 0x01bc, 0x001902e9);
    W(vop, 0x003c, 0x05000500);
    W(vop, 0x0040, 0x47c00000u);
    W(vop, 0x0048, 0x02cf04ff);
    W(vop, 0x004c, 0x02cf04ff);
    W(vop, 0x0050, 0x00000000);
    W(vop, 0x0030, 0x00000011);
    W(vop, 0x0008, sys);
    W(vop, 0x0000, 0x00000003);
}

static void pollit(void)
{
    int i;
    printf("    now sys=%08x sys1=%08x dsp0=%08x dsp1=%08x posth=%08x postv=%08x\n",
        R(vop,0x0008), R(vop,0x000c), R(vop,0x0004), R(vop,0x0014), R(vop,0x01b8), R(vop,0x01bc));
    for (i = 0; i < 8; i++) {
        usleep(20000);
        printf("    [%d] scan=%08x intr=%08x\n", i, R(vop,0x02a0), R(vop,0x0284));
    }
}

int main(void)
{
    int fd, i, j;
    uint32_t sysv[]  = {0x00802002, 0x00802003, 0x00803802, 0x00003802, 0x20801802};
    uint32_t sys1v[] = {0x00000000, 0x0003a000};
    uint32_t dsp0v[] = {0x03058890, 0x03058896, 0x00000000};
    uint32_t dsp1v[] = {0x00000000, 0x0000e400, 0x00e40000, 0x0000e440};

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    cru = mmap(NULL, 0x2000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
    vio = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VIO_PA);
    vop = mmap(NULL, 0x2000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    if (cru == MAP_FAILED || vio == MAP_FAILED || vop == MAP_FAILED) err(1, "mmap");

    common_prep();
    for (i = 0; i < (int)(sizeof(sysv)/sizeof(sysv[0])); i++) {
        for (j = 0; j < (int)(sizeof(dsp1v)/sizeof(dsp1v[0])); j++) {
            printf("\ntry sys=%08x sys1=%08x dsp0=%08x dsp1=%08x\n",
                sysv[i], sys1v[i % 2], dsp0v[i % 3], dsp1v[j]);
            mode_program(sysv[i], sys1v[i % 2], dsp0v[i % 3], dsp1v[j]);
            pollit();
            if (R(vop,0x02a0) != 0 || R(vop,0x0284) != 0)
                return 0;
        }
    }
    return 0;
}
