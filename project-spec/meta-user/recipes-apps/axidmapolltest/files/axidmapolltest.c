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

uint64_t buff[(2*1024)];
int main(int argc, char **argv)
{
    struct pollfd pfd;
    int ret;

    int fd = open("/dev/dma-0", O_WRONLY | O_NONBLOCK);
    if(fd == -1) {
        printf("Failed to open file\n");
    }

    pfd.fd = fd;
    pfd.events = ( POLLOUT | POLLWRNORM );
    for(int i = 0; i < 4; i++) {
        printf("APP: poll start\n");
        ret = poll(&pfd, (unsigned long)1, 5000);
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
