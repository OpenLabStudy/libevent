#include "uartManager.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>

int uartMakeNonblocking(int iFd)
{
    int iFlags = fcntl(iFd, F_GETFL, 0);
    if (iFlags < 0) 
        return -1;
    return fcntl(iFd, F_SETFL, iFlags | O_NONBLOCK);
}

/**
 * @brief UART 속성 설정 함수
 * @param iFd 파일 디스크립터
 * @param baudrate 원하는 Baudrate (예: 9600, 115200, 230400 등)
 * @return 0 성공, -1 실패
 */
int uartSetRaw(int iFd, int baudrate)
{
    struct termios stTermios;
    speed_t speed;

    //Baudrate 매핑
    switch (baudrate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
#ifdef B230400
        case 230400: speed = B230400; break;
#endif
#ifdef B460800
        case 460800: speed = B460800; break;
#endif
        default:
            fprintf(stderr, "Unsupported baudrate: %d\n", baudrate);
            return -1;
    }

    if (tcgetattr(iFd, &stTermios) < 0)
        return -1;

    cfmakeraw(&stTermios);
    cfsetispeed(&stTermios, speed);
    cfsetospeed(&stTermios, speed);

    stTermios.c_cflag &= ~PARENB;   // No parity
    stTermios.c_cflag &= ~CSTOPB;   // 1 stop bit
    stTermios.c_cflag &= ~CSIZE;
    stTermios.c_cflag |= CS8 | CLOCAL | CREAD; // 8 data bits, enable RX
    stTermios.c_cflag &= ~HUPCL;    // No hang-up on close

    stTermios.c_cc[VMIN]  = 1;
    stTermios.c_cc[VTIME] = 0;

    if (tcsetattr(iFd, TCSANOW, &stTermios) < 0)
        return -1;

    tcflush(iFd, TCIFLUSH);
    return 0;
}

/**
 * @brief UART 열기 함수
 * @param ctx UART context
 * @return 0 성공, -1 실패
 */
int uartOpen(UART_CTX* pstUartCtx)
{
    int iFd = open(pstUartCtx->pchDevPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (iFd < 0)
        return -1;

    // 원하는 Baudrate을 Context에서 가져오도록 변경
    if (uartSetRaw(iFd, pstUartCtx->iBaudrate) < 0) {
        close(iFd);
        return -1;
    }

    if (uartMakeNonblocking(iFd) < 0) {
        close(iFd);
        return -1;
    }

    pstUartCtx->iFd = iFd;
    return 0;
}

void uartClose(UART_CTX* pstUartCtx)
{
    if (pstUartCtx->iFd >= 0) {
        close(pstUartCtx->iFd);
        pstUartCtx->iFd = -1;
    }
}

void uartSend(UART_CTX* pstUartCtx, const char* pchMsg) 
{
    if (pstUartCtx->pstBev && pchMsg)
        bufferevent_write(pstUartCtx->pstBev, pchMsg, strlen(pchMsg));
}
