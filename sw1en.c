#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <dev/iicbus/iic.h>
#include <sys/ioctl.h>
int main(void){
    int fd=open("/dev/iic2",2);
    struct iic_msg m; struct iic_rdwr_data r;
    uint8_t buf[2];
    /* Read DCDC_EN */
    buf[0]=0x2f;
    m.slave=0x1b<<1; m.flags=0; m.len=1; m.buf=buf;
    r.msgs=&m; r.nmsgs=1; ioctl(fd,I2CRDWR,&r);
    uint8_t rdval[1]={0};
    m.slave=0x1b<<1; m.flags=1; m.len=1; m.buf=rdval;
    r.msgs=&m; r.nmsgs=1; ioctl(fd,I2CRDWR,&r);
    printf("DCDC_EN before: 0x%02x\n",rdval[0]);
    /* Enable SW1 (bit5) */
    buf[0]=0x2f; buf[1]=rdval[0]|(1<<5);
    m.slave=0x1b<<1; m.flags=0; m.len=2; m.buf=buf;
    r.msgs=&m; r.nmsgs=1; ioctl(fd,I2CRDWR,&r);
    printf("SW1 enabled\n");
    close(fd); return 0;
}
