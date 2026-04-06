#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void) {
  int fd = open("/dev/mem", O_RDONLY);
  /* GRF IOMUX for GPIO2 bank B (pins 8-15) */
  /* GRF base = 0xff320000 */
  /* GRF_GPIO2B_IOMUX = 0xff320000 + 0x0e8 */
  volatile uint32_t *grf = mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,0xff320000);
  printf("GRF_GPIO2B_IOMUX [0x0e8] = 0x%08x\n", grf[0x0e8/4]);
  /* GPIO2_B7 is bits[15:14] of this register */
  /* 0=GPIO, 1=HDMI_HPD, others=other functions */
  uint32_t v = grf[0x0e8/4];
  printf("GPIO2_B7 mux bits[15:14] = %d  ", (v>>14)&3);
  switch((v>>14)&3) {
  case 0: printf("(GPIO - HPD NOT routed to HDMI!)\n"); break;
  case 1: printf("(HDMI_HPD - correct)\n"); break;
  default: printf("(other function)\n"); break;
  }
  printf("GPIO2_B6 mux bits[13:12] = %d\n", (v>>12)&3);
  printf("GPIO2_B5 mux bits[11:10] = %d\n", (v>>10)&3);
  printf("Full GPIO2B_IOMUX = 0x%08x\n", v);
  close(fd);
  return 0;
}
