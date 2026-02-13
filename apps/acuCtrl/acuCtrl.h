#pragma once

#include <stdint.h>
#include <sys/time.h>

/* forward declarations to keep headers light */
struct event;
typedef struct _IO_CHANNEL IO_CHANNEL;

/* ========================================================================== */
/* ACU STATE                                                                  */
/* ========================================================================== */
typedef enum {
    ACU_STATE_IDLE = 0,
    ACU_STATE_WAIT_RESPONSE
} ACU_STATE;

/* ========================================================================== */
/* COMMAND STATE (ACU 내부 설정값)                                            */
/* ========================================================================== */
typedef struct {
    char    chSendOnOff;
    char    chAcuMode;
    double  dAzOffset;
    double  dElOffset;
} COMMAND_STATE;

/* ========================================================================== */
/* Pending command (UART 응답 매칭용)                                         */
/* ========================================================================== */
typedef struct {
    char            chInUse;
    unsigned short  unCmd;
    unsigned int    uiReqId;
    IO_CHANNEL*     pstUdsIo;
} ACU_PENDING_CMD;

/* ========================================================================== */
/* ACU CONTEXT                                                                */
/* ========================================================================== */
typedef struct {
    ACU_STATE        eAcuState;

    /* 원본 acuCtrl.c에서 사용하던 req 관리 값들 */
    unsigned int     uiReqId;
    unsigned int     uiLocalReqId;
    unsigned short   unCmd;

    ACU_PENDING_CMD  stPending;

    struct event*    pstTimeoutEvt;
    char             chIsUartAlive;

    /* AZ/EL Polling 관련 */
    char             chIsSendCommand;
    struct timeval   stLastAzElRxTime;

    COMMAND_STATE    stCommandState;
} ACU_CTRL_CTX;

int run(char *pchUartPath);
