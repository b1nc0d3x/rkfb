#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void) {
  int fd = open("/dev/mem", O_RDWR);
  volatile uint32_t *grf = mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0xff320000);
  volatile uint32_t *gpio2 = mmap(NULL,0x100,PROT_READ,MAP_SHARED,fd,0xff780000);

  printf("Before: GPIO2B_IOMUX = 0x%08x\n", grf[0x0e8/4]);
  printf("Before: GPIO2_DR bit15 = %d\n", (gpio2[0]>>15)&1);

  /* Set GPIO2_B7 to HDMI_HPD (function 1): bits[15:14] = 01 */
  /* Hiword mask write: mask bits[15:14] in upper word, val=01 in lower */
  grf[0x0e8/4] = (0x3u<<30) | (0x1u<<14);

  printf("After:  GPIO2B_IOMUX = 0x%08x\n", grf[0x0e8/4]);

  /* Now read HPD via HDMI PHY */
  volatile uint32_t *hdmi_grf = mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,0xff320000);
  volatile uint32_t *grf_status = (volatile uint32_t *)((char*)grf + 0x04e4);
  printf("GRF_SOC_STATUS1 [0x04e4] = 0x%08x  HPD bit14=%d\n",
	 *grf_status, (*grf_status>>14)&1);

  /* Poll for 5 seconds */
  printf("Polling GPIO2 bit15 for 5s...\n");
  for(int i=0;i<50;i++) {
    printf("  [%d] GPIO2_DR=0x%08x bit15=%d  GRF_STATUS1=0x%08x\n",
	   i, gpio2[0], (gpio2[0]>>15)&1, grf[0x04e4/4]);
    usleep(100000);
    if((gpio2[0]>>15)&1) { printf("  HPD HIGH!\n"); break; }
  }

  close(fd);
  return 0;
}
