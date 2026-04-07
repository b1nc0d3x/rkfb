#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint8_t *h = mmap(NULL,0x20000,PROT_READ,MAP_SHARED,fd,0xff940000);
    printf("Full HDMI dump [0x0000-0x00ff]:\n");
    for(int i=0;i<0x100;i++){
        if(i%16==0) printf("\n[%04x]",i);
        printf(" %02x",h[i]);
    }
    printf("\n\nHDMI [0x3000-0x30ff] (PHY interface):\n");
    for(int i=0;i<0x100;i++){
        if(i%16==0) printf("\n[%04x]",0x3000+i);
        printf(" %02x",h[0x3000+i]);
    }
    printf("\n\nHDMI [0x4000-0x40ff] (MC):\n");
    for(int i=0;i<0x100;i++){
        if(i%16==0) printf("\n[%04x]",0x4000+i);
        printf(" %02x",h[0x4000+i]);
    }
    printf("\n\nHDMI [0x0800-0x08ff] (VP):\n");
    for(int i=0;i<0x100;i++){
        if(i%16==0) printf("\n[%04x]",0x0800+i);
        printf(" %02x",h[0x0800+i]);
    }
    printf("\n\nHDMI [0x1000-0x10ff] (FC):\n");
    for(int i=0;i<0x100;i++){
        if(i%16==0) printf("\n[%04x]",0x1000+i);
        printf(" %02x",h[0x1000+i]);
    }
    printf("\n");
    close(fd);
    return 0;
}
