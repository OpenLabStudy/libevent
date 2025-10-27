#ifndef UART_EVENT_H
#define UART_EVENT_H

#include "uartTypes.h"

void uartEventInit(UART_CTX* pstUartCtx);
void uartEventCleanup(UART_CTX* pstUartCtx);
void uartEventAttach(UART_CTX* pstUartCtx);
void uartEventScheduleReopen(UART_CTX* pstUartCtx);

#endif
