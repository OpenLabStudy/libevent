#pragma once

/*
 * 기능별 파일 분리를 위한 내부 공용 헤더
 * - 외부 공개 API는 acuCtrl.h에만 둔다
 */

#include <stdint.h>

#include "acuCtrl.h"
#include "cmdRegistry.h"   /* COMMAND_PATH */
#include "eventEngine.h"   /* IO_CHANNEL / EVENT_ENGINE */

/* Response codes (원본 유지) */
#define RESP_OK        0x01
#define RESP_FAIL      0x00
#define RESP_BUSY      0x02
#define RESP_TIMEOUT   0x03
#define RESP_INTERNAL  0x04

#define ACU_UART_MONITORING_MSEC 400

/* Logic */
void initAcuCtrlCtx(ACU_CTRL_CTX* pstAcuCtrlCtx);
COMMAND_PATH decideProcessingPath(unsigned short unCmd);
void createAcuUartData(ACU_CTRL_CTX* pstAcuCtrlCtx,
                       unsigned short unCmd, char* pchCmdData,
                       char* pchOut, unsigned int* pOutLen);

/* UART callbacks */
void uartWriteCallback(int iFd, short nEvent, void *pvData);
void uartReadCallback(int iFd, short nEvent, void *pvData);

/* UDS callbacks */
void acuAzElPollingCb(int iFd, short nEvent, void *pvData);
void commandEventCb(int iFd, short nEvent, void *pvData);
void writeNone(int iFd, short nEvent, void* pvData);
void recvAzElFromSensorFusion(int iFd, short nEvent, void* pvData);
void sendCurrAzElToTC(int iFd, short nEvent, void* pvData);
