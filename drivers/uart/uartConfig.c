#include "uartConfig.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/**
 * @brief FD를 Non-Blocking 모드로 전환
 */
int uartMakeNonblocking(int iFd)
{
    int iFlags = fcntl(iFd, F_GETFL, 0);
    if (iFlags < 0)
        return -1;

    return fcntl(iFd, F_SETFL, iFlags | O_NONBLOCK);
}

/**
 * @brief UART를 raw 모드 + 속도 설정
 */
int uartSetRaw(int iFd, int baudrate)
{
    struct termios stTermios;
    speed_t speed;

    switch (baudrate) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
#ifdef B230400
        case 230400: speed = B230400; break;
#endif
#ifdef B460800
        case 460800: speed = B460800; break;
#endif
        default:
            fprintf(stderr, "[UART] Unsupported baudrate: %d\n", baudrate);
            return -1;
    }

    if (tcgetattr(iFd, &stTermios) < 0)
        return -1;

    cfmakeraw(&stTermios);

    cfsetispeed(&stTermios, speed);
    cfsetospeed(&stTermios, speed);

    stTermios.c_cflag &= ~PARENB;
    stTermios.c_cflag &= ~CSTOPB;
    stTermios.c_cflag &= ~CSIZE;
    stTermios.c_cflag |= CS8 | CLOCAL | CREAD;
    stTermios.c_cflag &= ~HUPCL;

    stTermios.c_cc[VMIN]  = 1;
    stTermios.c_cc[VTIME] = 0;

    if (tcsetattr(iFd, TCSANOW, &stTermios) < 0)
        return -1;

    tcflush(iFd, TCIFLUSH);
    return 0;
}

/**
 * @brief UART 오픈 + 설정
 */
int uartOpen(UART_CTX *pstUartCtx)
{
    if (!pstUartCtx || !pstUartCtx->pchDevPath)
        return -1;
    
    int iFd = open(pstUartCtx->pchDevPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (iFd < 0) {
        fprintf(stderr, "[UART] open(%s) failed: %s\n",
                pstUartCtx->pchDevPath, strerror(errno));
        return -1;
    }

    if (uartSetRaw(iFd, pstUartCtx->iBaudrate) < 0) {
        close(iFd);
        return -1;
    }

    tcflush(iFd, TCIOFLUSH);

    if (uartMakeNonblocking(iFd) < 0) {
        close(iFd);
        return -1;
    }

    pstUartCtx->iFd = iFd;
    return 0;
}

/**
 * @brief UART 닫기
 */
void uartClose(UART_CTX *pstUartCtx)
{
    if (!pstUartCtx)
        return;

    if (pstUartCtx->iFd >= 0) {
        close(pstUartCtx->iFd);
        pstUartCtx->iFd = -1;
    }
}
