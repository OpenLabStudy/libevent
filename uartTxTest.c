// 터미널에서 socat -d -d pty,raw,echo=0 pty,raw,echo=0 실행후 출력되는 아래 문구의 포트를 이용
// 2025/09/30 16:06:59 socat[16225] N PTY is /dev/pts/4
// 2025/09/30 16:06:59 socat[16225] N PTY is /dev/pts/5
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "사용법: %s /dev/pts/X\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return 1;
    }

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag = 0;
    tty.c_lflag = 0;
    tty.c_oflag = 0;

    tcsetattr(fd, TCSANOW, &tty);

    const char *msg = "Hello UART!\n";
    for (int i = 0; i < 5; i++) {
        write(fd, msg, strlen(msg));
        printf("Sent: %s", msg);
        sleep(1);
    }

    close(fd);
    return 0;
}
