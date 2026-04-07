#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "rkfb_ioctl.h"
int main(void){
    int fd=open("/dev/rkfb0",2);
    struct rkfb_fill f; f.pixel=0x00ff0000;  /* red */
    ioctl(fd,RKFB_CLEAR,&f);
    printf("filled red\n");

    close(fd);

    return 0;
}
