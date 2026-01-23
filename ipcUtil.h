#ifndef IPC_UTIL_H
#define IPC_UTIL_H

#include "icdCommand.h"
#include "cmdRegistry.h"
#include "netUds.h"
#include "netCore.h"
#include "ioChannelUtil.h"

typedef enum {
    ACU_CMD_OK = 0,
    ACU_CMD_INVALID,
    ACU_CMD_BUSY,
    ACU_CMD_PARAM_ERROR,
    ACU_CMD_INTERNAL_ERROR
} IPC_CMD_RESULT;

/* ========================================================================== */
/* Response code (UDS payload result)                                          */
/*  - 맞는 코드값은 ICD에 맞게 조정하세요                                     */
/* ========================================================================== */
#define RESP_OK        0x01
#define RESP_FAIL      0x00
#define RESP_BUSY      0x02
#define RESP_TIMEOUT   0x03
#define RESP_INTERNAL  0x04

typedef struct {
    unsigned short  unCmd;
    IPC_CMD_RESULT  eResult;

    /* 실제 설정을 위한 파라미터 */
    union {
        struct{
            char chBit;
        } stBit;

        struct {
            char chTrackingSelect;
        } stTrackingSelect;

        struct {
            char chTrackingStartStop;
        } stTrackingControl;

        struct {
            double dAz;
            double dEl;
        } stPositionerAzElSet;

        struct {
            char chSendOnOff;
        } stPositionerAzElSendCtrl;

        struct {
            char chAcuMode;
        } stAcuMode;

        struct {
            int iAzOffset;
	        int iElOffset;
        } stAzElOffsetSet;

        struct {
            char chWaitOnOff;
            double dStandbyAz;
            double dStandbyEl;
        } stAutoTrackingWait;
    } u;
} IPC_CMD_CTX;


/* Worker ID → MSG_ID 변환 */
int ipcBuildMsgIdFromWorker(int iWorkerId, void* pvMsgId);

/* Worker Register 전송 */
void ipcSendWorkerRegister(IO_CHANNEL* pstIoChannel, unsigned char uchWorkerType);
int ipcHandleCommand(unsigned short unCmd, const unsigned char* pReqPayload, IPC_CMD_CTX* pstCmdCtx);
void sendUdsResponse(IO_CHANNEL* pstUdsIo, unsigned short unCmd, unsigned int uiReqId,
                               const unsigned char* pPayload, unsigned int uiPayloadMax);
#endif
