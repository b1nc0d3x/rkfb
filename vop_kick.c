#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA   0xff760000UL
#define VIO_PA   0xff770000UL
#define WEST_PA  0xff900000UL
#define EAST_PA  0xff8f0000UL

static volatile uint32_t *cru, *vio, *west, *east;
static inline void dsb(void){ __asm volatile("dsb sy" ::: "memory"); }
static inline uint32_t R(volatile uint32_t *b, uint32_t o){ return b[o/4]; }
static inline void W(volatile uint32_t *b, uint32_t o, uint32_t v){ b[o/4]=v; dsb(); }
static inline void CHW(uint32_t o, uint32_t m, uint32_t v){ W(cru,o,(m<<16)|(v&m)); }

static void program_vop(volatile uint32_t *v, uint32_t fb, uint32_t sys)
{
    uint32_t d = R(v, 0x0004);
    d &= ~0xfu;
    W(v, 0x0004, d);
    W(v, 0x0010, 0x00000000);
    W(v, 0x0188, 0x06710027);
    W(v, 0x018c, 0x01040604);
    W(v, 0x0190, 0x02ed0004);
    W(v, 0x0194, 0x001902e9);
    W(v, 0x003c, 0x05000500);
    W(v, 0x0040, fb);
    W(v, 0x0048, 0x02cf04ff);
    W(v, 0x004c, 0x02cf04ff);
    W(v, 0x0050, 0x00000000);
    W(v, 0x0030, 0x00000011);
    W(v, 0x0008, sys);
    W(v, 0x0000, 0x00000003);
}

static void poll_one(const char *name, volatile uint32_t *v)
{
    int i;
    printf("%s: SYS=%08x DSP=%08x WIN0=%08x SCAN=%08x INTR=%08x\n",
        name, R(v,0x0008), R(v,0x0004), R(v,0x0030), R(v,0x02a0), R(v,0x0284));
    for (i = 0; i < 8; i++) {
        usleep(20000);
        printf("  [%d] scan=%08x intr=%08x\n", i, R(v,0x02a0), R(v,0x0284));
    }
}

int main(void)
{
    int fd, i;
    uint32_t west_sys[] = {
        0x00000002,
        0x00002002,
        0x00800002,
        0x00802002,
        0x00802003,
        0x00802006,
        0x00882002,
        0x00883802,
        0x00803802,
        0x00003802,
    };

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    cru  = mmap(NULL, 0x2000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
    vio  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VIO_PA);
    west = mmap(NULL, 0x2000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, WEST_PA);
    east = mmap(NULL, 0x2000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, EAST_PA);
    if (cru == MAP_FAILED || vio == MAP_FAILED || west == MAP_FAILED || east == MAP_FAILED)
        err(1, "mmap");

    CHW(0x022c, (1u<<0)|(1u<<1)|(1u<<3)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<12), 0);
    CHW(0x0228, (1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<13), 0);
    CHW(0x0270, (1u<<2)|(1u<<3)|(1u<<6)|(1u<<7), 0);
    CHW(0x0274, (1u<<0)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<12), 0);
    /* release display-fabric resets discovered from upstream reset ids */
    CHW(0x033c, (1u<<0)|(1u<<7), 0);
    CHW(0x0340, (1u<<2)|(1u<<4), 0);
    CHW(0x0344, 0xffu, 0);
    CHW(0x00bc, (3u<<6)|0x1fu|(0x1fu<<8), (2u<<6)|1u|(3u<<8));
    CHW(0x00c0, (3u<<6)|0x1fu|(0x1fu<<8), (2u<<6)|1u|(3u<<8));
    CHW(0x00c4, (3u<<8)|0xffu, (1u<<8)|7u);
    CHW(0x00c8, (3u<<8)|0xffu, (1u<<8)|7u);

    printf("CLKSEL47=%08x CLKSEL48=%08x CLKSEL49=%08x CLKSEL50=%08x\n",
        R(cru,0x00bc), R(cru,0x00c0), R(cru,0x00c4), R(cru,0x00c8));

    for (i = 0; i < (int)(sizeof(west_sys)/sizeof(west_sys[0])); i++) {
        printf("\nwest try %d sys=%08x route=west\n", i, west_sys[i]);
        W(vio, 0x0250, (0x00ffu<<16) | 0x0000u);
        program_vop(west, 0x47c00000u, west_sys[i]);
        poll_one("west", west);
        if (R(west, 0x02a0) != 0)
            break;
    }

    for (i = 0; i < (int)(sizeof(west_sys)/sizeof(west_sys[0])); i++) {
        printf("\neast try %d sys=%08x route=east\n", i, west_sys[i]);
        W(vio, 0x0250, (0x00ffu<<16) | 0x0040u);
        program_vop(east, 0x47c00000u, west_sys[i]);
        poll_one("east", east);
        if (R(east, 0x02a0) != 0)
            break;
    }
    return 0;
}
