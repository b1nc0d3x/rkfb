#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint32_t *cru = mmap(NULL, 0x1000, PROT_READ,
        MAP_SHARED, fd, 0xff760000);
    /* VPLL_CON0-3 at CRU + 0x0020-0x002c */
    printf("VPLL_CON0 [0x0020] = 0x%08x\n", cru[0x0020/4]);
    printf("VPLL_CON1 [0x0024] = 0x%08x\n", cru[0x0024/4]);
    printf("VPLL_CON2 [0x0028] = 0x%08x\n", cru[0x0028/4]);
    printf("VPLL_CON3 [0x002c] = 0x%08x\n", cru[0x002c/4]);
    /* CLKSEL49 — HDMI pixel clock source */
    printf("CLKSEL49  [0x00c4] = 0x%08x\n", cru[0x00c4/4]);
    /* CLKGATE20 — pclk_hdmi_ctrl */
    printf("CLKGATE20 [0x0250] = 0x%08x\n", cru[0x0250/4]);
	close(fd); 
   return 0;
}
