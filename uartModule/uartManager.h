#ifndef UART_MANAGER_H
#define UART_MANAGER_H

#include "uartTypes.h"

int uartOpen(UART_CTX* pstUartCtx);
void uartClose(UART_CTX* pstUartCtx);
int uartSetRaw(int iFd, int baudrate);
int uartMakeNonblocking(int iFd);
void uartSend(UART_CTX* pstUartCtx, const char* pchMsg);

#endif
