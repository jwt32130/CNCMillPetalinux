/*
* Copyright (C) 2013 - 2016  Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without restriction,
* including without limitation the rights to use, copy, modify, merge,
* publish, distribute, sublicense, and/or sell copies of the Software,
* and to permit persons to whom the Software is furnished to do so,
* subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in this
* Software without prior written authorization from Xilinx.
*
*/

#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
// #include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <memory.h>

#define buflength 2048
uint64_t buff[buflength];
int main(int argc, char **argv)
{
    struct pollfd pfd;
    int ret;

    int rst_fd = open("/sys/class/cncController/cncC0/stop_reset", O_WRONLY | O_NONBLOCK);
    if(rst_fd == -1) {
        printf("Failed to open sysfs file\n");
    }
    ret = write(rst_fd, "1", 1);

    int fd = open("/dev/dma-0", O_WRONLY | O_NONBLOCK);
    if(fd == -1) {
        printf("Failed to open file\n");
    }
    printf("File open\n");
    uint8_t m1 = 0x03;
    uint8_t m2 = 0x03;
    uint8_t m3 = 0x03;
    int timer = (720*2)/20; //16000000/50mhz ~.3seconds
    
    for(int i = 0; i < buflength; i++) {
        buff[i] = (uint64_t)timer + ((uint64_t)m1 << 24) + ((uint64_t)m2 << 32) + ((uint64_t)m3 << 40);
    }

    pfd.fd = fd;
    pfd.events = ( POLLOUT | POLLWRNORM );
    for(int i = 0; i < 100; i++) {
        printf("APP: poll start\n");
        ret = poll(&pfd, (unsigned long)1, 20000);
        printf("%x:%x\n", ret, pfd.revents);
        if(ret < 0) {
            printf("Error in polling\n");
            assert(0);
        }
        if((pfd.revents & POLLOUT) == POLLOUT) {
            printf("APP: driver ready to write to\n");
            int wcount = write(fd, (void*)buff, sizeof(buff));
        }
        // sleep(1);
    }   
    close(fd);
    return 0;
}
