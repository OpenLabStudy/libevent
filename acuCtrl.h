
/* ========================================================================== */
/* ACU STATE                                                                  */
/* ========================================================================== */
typedef enum {
    ACU_STATE_IDLE = 0,
    ACU_STATE_WAIT_RESPONSE
} ACU_STATE;

/* ========================================================================== */
/* COMMAND STATE (ACU 내부 설정값)                                             */
/* ========================================================================== */
typedef struct {
    char    chSendOnOff;
    char    chAcuMode;
    double  dAzOffset;
    double  dElOffset;
} COMMAND_STATE;

/* ========================================================================== */
/* Pending command (UART 응답 매칭용)                                         */
/*  - "현재 1개 pending"만 처리 (필요시 큐로 확장)                            */
/* ========================================================================== */
typedef struct {
    int             bInUse;
    unsigned short  unCmd;
    unsigned int    uiReqId;
    IO_CHANNEL*     pstUdsIo;   /* 응답을 보낼 UDS 채널 */
} ACU_PENDING_CMD;

/* ========================================================================== */
/* ACU CONTEXT (전역 대체)                                                     */
/* ========================================================================== */
typedef struct {
    ACU_STATE        eState;
    ACU_PENDING_CMD  stPending;

    struct event*    pstTimeoutEvt; /* 200ms timer (reused) */

    int              iIsUartAlive;     /* 1: 정상, 0: 비정상 */

    /* ============================= */
    /* [ADDED] AZ/EL Polling 관련   */
    /* ============================= */
    int              iIsSendCommand;     // polling 명령 전송 중 여부
    struct timeval   stLastAzElRxTime;         // 마지막 AZ/EL 수신 시간

    COMMAND_STATE    stCommandState;
} ACU_CTRL_CTX;

//acuCtrl.c, acuCtrl.h, acuCtrlForTest.c, acuUartProc.c, acuUartProc.h, acuUds1Proc.c, acuUds1Proc.h, acuUds3Proc.c, acuUds3Proc.h, acuUds4Proc.c, acuUds4Proc.h, acuUtil.c, acuUtil.h