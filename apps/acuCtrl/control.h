#pragma once

#include <stdint.h>
#include <sys/time.h>

/* forward declare */
typedef struct _IO_CHANNEL IO_CHANNEL;

/* ========================================================================== */
/* ACU STATE                                                                  */
/* ========================================================================== */

typedef enum {
    ACU_STATE_IDLE = 0,
    ACU_STATE_WAIT_RESPONSE
} ACU_STATE;

/* ========================================================================== */
/* ACU MODE                                                                   */
/* ========================================================================== */

typedef enum {
    ACU_MODE_NONE = 0,
    POSITION = 1,
    RATE = 2
} ACU_MODE;

/* ========================================================================== */
/* SEND ON/OFF                                                                */
/* ========================================================================== */

typedef enum {
    AZ_EL_SEND_OFF = 0,
    AZ_EL_SEND_ON  = 1
} AZ_EL_SEND_MODE;

/* ========================================================================== */
/* PENDING COMMAND                                                            */
/* ========================================================================== */

typedef struct {
    unsigned char   uchInUse;
    unsigned short  unCmd;
    unsigned int    uiReqId;
    IO_CHANNEL*     pstUdsIo;  /* runtime에서만 사용 */
} ACU_PENDING_CMD;

/* ========================================================================== */
/* COMMAND STATE                                                              */
/* ========================================================================== */

typedef struct {
    unsigned char   uchAcuMode;
    unsigned char   uchSendOnOff;
    double          dAzOffset;
    double          dElOffset;
} ACU_COMMAND_STATE;

/* ========================================================================== */
/* ACU CTRL CONTEXT                                                           */
/* ========================================================================== */

typedef struct {
    ACU_STATE           eAcuState;
    ACU_PENDING_CMD     stPending;
    ACU_COMMAND_STATE   stCommandState;

    int                 iIsUartAlive;
    int                 iIsSendCommand;

    struct timeval      stLastAzElRxTime;
} ACU_CTRL_CTX;

/* runtime entry */
int run(char* uartPath);
