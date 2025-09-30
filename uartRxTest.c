// 터미널에서 socat -d -d pty,raw,echo=0 pty,raw,echo=0 실행후 출력되는 아래 문구의 포트를 이용
// 2025/09/30 16:06:59 socat[16225] N PTY is /dev/pts/4
// 2025/09/30 16:06:59 socat[16225] N PTY is /dev/pts/5
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "사용법: %s /dev/pts/X\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tcsetattr(fd, TCSANOW, &tty);

    char buf[100];
    while (1) {
        int n = read(fd, buf, sizeof(buf)-1);
        if (n > 0) {
            buf[n] = '\0';
            printf("Received: %s", buf);
        }
    }

    close(fd);
    return 0;
}
