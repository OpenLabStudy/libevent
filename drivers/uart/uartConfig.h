#ifndef UART_CONFIG_H
#define UART_CONFIG_H

#include <termios.h>

/**
 * @brief UART 설정 컨텍스트
 */
typedef struct {
    const char *pchDevPath;   /* UART 디바이스 경로 */
    int         iFd;          /* UART FD */
    int         iBackoffMsec; /* 재연결 backoff (필요시 사용) */
    int         iBaudrate;    /* Baudrate */
} UART_CTX;

int uartMakeNonblocking(int iFd);
int uartSetRaw(int iFd, int baudrate);

int uartOpen(UART_CTX *pstUartCtx);
void uartClose(UART_CTX *pstUartCtx);

#endif /* UART_CONFIG_H */
