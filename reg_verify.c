/*
 * reg_verify.c - Write a register and immediately read it back in a tight loop
 * to determine if it's being reset by the kernel between write and readback.
 *
 * Tests: VIO GRF SOC_CON20 bit6, VOP SYS_CTRL bit1, HDMI MC_PHYRSTZ
 *
 * Build: cc -o reg_verify reg_verify.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VIOGRF_PA  0xff770000UL
#define VOP_PA     0xff900000UL
#define HDMI_PA    0xff940000UL

int main(void)
{
	int fd;
	volatile uint32_t *viogrf, *vop;
	volatile uint8_t  *hdmi;

	fd = open("/dev/mem", O_RDWR);
	if (fd < 0) err(1, "open /dev/mem");

	viogrf = mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIOGRF_PA);
	vop    = mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
	hdmi   = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,HDMI_PA);
	if (viogrf==MAP_FAILED) err(1,"mmap VIO GRF");
	if (vop==MAP_FAILED)    err(1,"mmap VOP");
	if (hdmi==MAP_FAILED)   err(1,"mmap HDMI");

	/* Test 1: VIO GRF SOC_CON20 bit6 - should be sticky (no kernel driver owns it) */
	printf("=== Test 1: VIO GRF SOC_CON20 bit6 (HDMI mux) ===\n");
	printf("Before: 0x%08x\n", viogrf[0x250/4]);
	viogrf[0x250/4] = (1u<<22)|(1u<<6);   /* hiword: mask=bit6, val=bit6 */
	printf("After write (immediate): 0x%08x\n", viogrf[0x250/4]);
	usleep(100000);
	printf("After 100ms:             0x%08x\n", viogrf[0x250/4]);
	usleep(1000000);
	printf("After 1s:                0x%08x  (bit6=%d)\n\n",
	    viogrf[0x250/4], (viogrf[0x250/4]>>6)&1);

	/* Test 2: HDMI MC_PHYRSTZ - kernel HDMI driver may own this */
	printf("=== Test 2: HDMI MC_PHYRSTZ [0x4005] ===\n");
	printf("Before: 0x%02x\n", hdmi[0x4005]);
	hdmi[0x4001] = 0x00;  /* MC_CLKDIS: all on */
	hdmi[0x4002] = 0xff;  /* MC_SWRSTZREQ: all released */
	usleep(5000);
	hdmi[0x4005] = 0x01;
	printf("After write (immediate): 0x%02x\n", hdmi[0x4005]);
	usleep(10000);
	printf("After 10ms:              0x%02x\n", hdmi[0x4005]);
	usleep(100000);
	printf("After 100ms:             0x%02x\n", hdmi[0x4005]);
	usleep(1000000);
	printf("After 1s:                0x%02x  (want 0x01)\n\n", hdmi[0x4005]);

	/* Test 3: VOP SYS_CTRL - read live value after REG_CFG_DONE */
	printf("=== Test 3: VOP SYS_CTRL bit1 (hdmi_dclk_en) ===\n");
	printf("Before: 0x%08x\n", vop[0x0008/4]);
	{
		uint32_t sc = vop[0x0008/4];
		sc &= ~(1u<<11);
		sc |=  (1u<<1);
		vop[0x0008/4] = sc;
		vop[0x0000/4] = 0x01;  /* REG_CFG_DONE */
	}
	printf("After write (immediate): 0x%08x\n", vop[0x0008/4]);
	usleep(20000);
	printf("After 20ms (post-vsync): 0x%08x\n", vop[0x0008/4]);
	usleep(100000);
	printf("After 100ms:             0x%08x\n", vop[0x0008/4]);
	usleep(1000000);
	printf("After 1s:                0x%08x  (want bit1=1, bit11=0)\n\n",
	    vop[0x0008/4]);

	/* Test 4: Does HDMI MC_PHYRSTZ readback even work? */
	/* Write known pattern to a writable HDMI reg and see if it sticks */
	printf("=== Test 4: HDMI PHY_CONF0 [0x3000] write persistence ===\n");
	printf("Before: 0x%02x\n", hdmi[0x3000]);
	hdmi[0x3000] = 0xd2;
	printf("After write (immediate): 0x%02x\n", hdmi[0x3000]);
	usleep(10000);
	printf("After 10ms:              0x%02x\n", hdmi[0x3000]);
	hdmi[0x4005] = 0x01;
	printf("MC_PHYRSTZ after write:  0x%02x\n", hdmi[0x4005]);
	usleep(10000);
	printf("MC_PHYRSTZ after 10ms:   0x%02x\n", hdmi[0x4005]);
	usleep(100000);
	printf("MC_PHYRSTZ after 100ms:  0x%02x  (if 0x00, kernel is resetting it)\n",
	    hdmi[0x4005]);

	close(fd);
	return 0;
}
