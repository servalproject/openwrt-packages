/*
  Utility to flash a Dragino2 when it connects to a given serial port.
  Looks for Uboot prompt. If it is showing the default uboot (that can be
  interrupted with any key), then it reflashes uboot and resets the board.
  When it sees a board with the correct uboot, it reflashes the mesh extender
  firmware.  When that completes, and the board is booting, it runs xclock
  to raise a visual alert for the operator to remove the mesh extender and
  insert the next one.

*/

#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

int fd;
char line[1024];
int ofs = 0;

int done_on_reset = 0;
int ignore_uboot = 0;

long long gettime_ms()
{
    struct timeval nowtv;
    // If gettimeofday() fails or returns an invalid value, all else is lost!
    if (gettimeofday(&nowtv, NULL) == -1)
        return -1;
    if (nowtv.tv_sec < 0 || nowtv.tv_usec < 0 || nowtv.tv_usec >= 1000000)
        return -1;
    return nowtv.tv_sec * 1000LL + nowtv.tv_usec / 1000;
}


int set_nonblock(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL, NULL)) == -1)
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        return -1;
    return 0;
}

int setup_serial_port(int fd, int baud)
{
    struct termios t;

    if (tcgetattr(fd, &t)) return -1;

    speed_t baud_rate;
    switch(baud){
        case 0: baud_rate = B0; break;
        case 50: baud_rate = B50; break;
        case 75: baud_rate = B75; break;
        case 110: baud_rate = B110; break;
        case 134: baud_rate = B134; break;
        case 150: baud_rate = B150; break;
        case 200: baud_rate = B200; break;
        case 300: baud_rate = B300; break;
        case 600: baud_rate = B600; break;
        case 1200: baud_rate = B1200; break;
        case 1800: baud_rate = B1800; break;
        case 2400: baud_rate = B2400; break;
        case 4800: baud_rate = B4800; break;
        case 9600: baud_rate = B9600; break;
        case 19200: baud_rate = B19200; break;
        case 38400: baud_rate = B38400; break;
        default:
        case 57600: baud_rate = B57600; break;
        case 115200: baud_rate = B115200; break;
        case 230400: baud_rate = B230400; break;
    }

    if (cfsetospeed(&t, baud_rate)) return -1;
    if (cfsetispeed(&t, baud_rate)) return -1;

    // 8N1
    t.c_cflag &= ~PARENB;
    t.c_cflag &= ~CSTOPB;
    t.c_cflag &= ~CSIZE;
    t.c_cflag |= CS8;

    t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO | ECHOE);
    /* Noncanonical mode, disable signals, extended
       input processing, and software flow control and echoing */

    t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
                   INPCK | ISTRIP | IXON | IXOFF | IXANY | PARMRK);
    /* Disable special handling of CR, NL, and BREAK.
       No 8th-bit stripping or parity error handling.
       Disable START/STOP output flow control. */

    // Enable/disable CTS/RTS flow control
#ifndef CNEW_RTSCTS
    t.c_cflag &= ~CRTSCTS;
#else
    t.c_cflag &= ~CNEW_RTSCTS;
#endif

    // no output processing
    t.c_oflag &= ~OPOST;

    if (tcsetattr(fd, TCSANOW, &t))
        return -1;

    set_nonblock(fd);

    fprintf(stderr,"Set serial port to %dbps\n",baud);

    return 0;
}

int next_char(int fd)
{
    fsync(fd);
    int w=0;
    time_t timeout=time(0)+10;
    while(time(0)<timeout) {
        unsigned char buffer[2];
        int r=read(fd,(char *)buffer,1);
        if (r==1) {
            return buffer[0];
        } else { usleep(1000); w++; }
    }
    return -1;
}

int main(int argc,char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <serial port>\n", argv[0]);
        exit(3);
    }

    fprintf(stderr,"Opening serial port...\n");

    int fd=open(argv[1],O_RDWR|O_NONBLOCK);
    if (fd==-1) {
        fprintf(stderr,"Could not open serial port '%s'\n",argv[1]);
        exit(-1);
    }
    fprintf(stderr,"Setting serial port speed...\n");
    if (setup_serial_port(fd,115200)) {
        fprintf(stderr,"Could not setup serial port '%s'\n",argv[1]);
        exit(-1);
    }

    long long last_time=gettime_ms();

    fprintf(stderr,"Ready and waiting...\n");
    
    while(1) {
        // Get the next character
        int c = next_char(fd);

        // If it's been a while since the last character, print a dot
        long long now = gettime_ms();
        if (now - last_time > 500) {
            printf(".");
            fflush(stdout);
            last_time = now;
        }

        // Make sure we have a non-null character
        if (c > 0) {
            // Is this an ordinary, printable character?
            if (c >= ' ')
            {
                // Append it to the buffer
                line[ofs++] = c;
                //fprintf(stderr, line);

                // Is the device prompting us for the boot console?
                if (!strncmp(line, "Hit any key to stop autoboot:  4", 32)) { // String is 32 chars long
                    fprintf(stderr, ">>> ignore_uboot=%d in uboot interrupt sequence\n", ignore_uboot);
                    if (!ignore_uboot)
                    {
                        // Wait a moment for the device to start accepting keystrokes
                        usleep(10000); // 10ms
                        
                        // Print a space and wait for the U-Boot console to load
                        write(fd, " ", 2);
                        fsync(fd); // Flush the command
                        usleep(3000000); // Wait three seconds
                    
                        // Flush the command
                        write(fd, "\r", 1);
                    
                        char* command="\r\ntftp 82000000 openwrt-dragino2.bin; erase 0x9f050000 +$filesize; cp.b 0x82000000 0x9f050000 $filesize; setenv bootcmd bootm 0x9f050000; saveenv; reset\r\n";
                        for(int i = 0; command[i]; i++) {
                            write(fd, &command[i], 1); // Write the character
                            usleep(2000); // Wait for it to be transmitted
                        }
                    
                        // Set a flag to mark reboot as the end of this board's flashing process
                        done_on_reset = 1;
                        fprintf(stderr,">>> Asserting done_on_reset=1 in uboot interrupt sequence\n");
                    }
                    else
                    {
                        ignore_uboot=0;
                        fprintf(stderr,">>> Clearing ignore_uboot=0 in uboot interrupt sequence\n");
                    }

                    // Null-terminate the buffer and reset the offset
                    line[0] = 0;
                    ofs = 0;
                }
            }
            // If this is a newline character, process the entire line
            else if ((c=='\r')||(c=='\n'))
            {
                // Null-terminate the buffer
                line[ofs]=0;

                // Announce the line to the terminal
                if (ofs)
                    fprintf(stderr,"dor=%d, iu=%d: line is '%s'\n",
                            done_on_reset,ignore_uboot,line);
                fflush(stderr);

                // Is the device booting?
                if (!strcmp(line,"AP121-2MB (ar9330) U-boot"))
                {
                    fprintf(stderr,">>> done_on_reset=%d in uboot banner check\n",done_on_reset);

                    // Check if we were waiting for a reboot
                    if (done_on_reset) {
                        // Reset the ignore-reboot flags in prep for another board
                        done_on_reset=0;
                        ignore_uboot=1;
                        fprintf(stderr,">>> Asserting ignore_uboot=1 in uboot banner check\n");
                        fprintf(stderr,">>> Clearing done_on_reset=0 in uboot banner check\n");
                    }
                }

                // Has U-Boot said 'OK!' with sunshine in its eyes and a spring in its step?
                if (!strcmp(line,"OK!")) {
                    fprintf(stderr,">>> Sending carriage return\n");
                    write(fd,"reset\r",6);
                    if (done_on_reset) {
                        // Play the system bell
                        printf("\a");

                        // Reset the ignore-reboot flags in prep for another board
                        done_on_reset=0;
                        ignore_uboot=1;
                        fprintf(stderr,">>> Asserting ignore_uboot=1 in uboot banner check\n");
                        fprintf(stderr,">>> Clearing done_on_reset=0 in uboot banner check\n");
                    }
                }

                // Done processing the line, reset the offset
                ofs=0;
            }
        }
    }

    return 0;
}
