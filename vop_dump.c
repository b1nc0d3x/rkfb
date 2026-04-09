/*
 * vop_dump.c - Dump VOP registers to find scan state
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VOP_PA 0xff900000UL

static volatile uint32_t *g_vop;
#define VR(o) g_vop[(o)/4]

int main(void)
{
    int fd, i;
    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_vop = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    if (g_vop == MAP_FAILED) err(1, "mmap VOP");

    printf("VOP registers 0x0000-0x003F (system control):\n");
    for (i = 0; i < 0x40; i += 4)
        printf("  [0x%04x]: 0x%08x\n", i, VR(i));

    printf("\nVOP registers 0x0280-0x02CF (interrupt / status):\n");
    for (i = 0x0280; i < 0x02D0; i += 4)
        printf("  [0x%04x]: 0x%08x\n", i, VR(i));

    /* Also try potential scan position offsets */
    printf("\nPotential scan position candidates:\n");
    printf("  [0x01CC (STATUS0)]:   0x%08x\n", VR(0x01CC));
    printf("  [0x01D0 (STATUS1?)]:  0x%08x\n", VR(0x01D0));
    printf("  [0x0244]:             0x%08x\n", VR(0x0244));
    printf("  [0x0248]:             0x%08x\n", VR(0x0248));
    printf("  [0x02A0]:             0x%08x\n", VR(0x02A0));
    printf("  [0x02A4]:             0x%08x\n", VR(0x02A4));
    printf("  [0x02A8]:             0x%08x\n", VR(0x02A8));

    /* Read SYS_STATUS if it exists at 0x0024 */
    printf("\n  [0x0024 SYS_STATUS?]: 0x%08x\n", VR(0x0024));
    printf("  [0x0028]:             0x%08x\n", VR(0x0028));
    printf("  [0x002C]:             0x%08x\n", VR(0x002C));

    /* Read twice to see if anything changes */
    usleep(20000);
    printf("\nAfter 20ms - re-reading candidates:\n");
    printf("  [0x0024]: 0x%08x\n", VR(0x0024));
    printf("  [0x0028]: 0x%08x\n", VR(0x0028));
    printf("  [0x02A0]: 0x%08x\n", VR(0x02A0));
    printf("  [0x02A4]: 0x%08x\n", VR(0x02A4));
    printf("  [0x02A8]: 0x%08x\n", VR(0x02A8));
    printf("  [0x0244]: 0x%08x\n", VR(0x0244));
    printf("  [0x0284]: 0x%08x\n", VR(0x0284));

    close(fd);
    return 0;
}
