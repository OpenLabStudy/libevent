#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "acuCtrl.h"      // ACU_CTRL_CTX (통일)
#include "icdCommand.h"   // CMD_*, MSG_ID, frame helpers
#include "cmdRegistry.h"  // cmdDispatch
#include "acuUtil.h"      // moveAzElPosition/Rate, readAzElFromAcu, splitAcuDataString, endianChange
#include "timeUtil.h"     // timePackHMSms

/* Core는 libevent/evbuffer/FD를 몰라야 함.
 * Core 결과는 "Action list"로 runtime에 전달되고,
 * runtime이 action을 적용(evbuffer_add/event_add/event_active/engine routing)한다.
 */

typedef enum {
    ACU_ACT_NONE = 0,    
    ACU_ACT_UDS_WRITE_CURRENT,      /* 현재 UDS 채널(=commandEventCb를 호출한 IO_CHANNEL)로 응답 프레임을 write */    
    ACU_ACT_UART_REQUEST,           /* UART 채널(ACU_CTRL_UART)의 requestBuffer에 (COMMAND_PATH + payload) 넣고 requestEvent active */
    ACU_ACT_ENGINE_WORKER_RESPONSE, /* worker response 라우팅: eventEngineHandleWorkerResponse(engine, uartIo, reqSeq, frame, len) */
    ACU_ACT_PROTOCOL_ERROR,         /* 파싱 실패/이상 데이터: runtime이 로깅/드레인 정책 수행 */
} ACU_ACTION_TYPE;

typedef struct {
    ACU_ACTION_TYPE eAcuActionType;
    
    unsigned char   uchFrameBuffer[2048];/* UDS_WRITE_CURRENT / ENGINE_WORKER_RESPONSE 공통: 전송할 frame bytes */
    unsigned int    uiFrameLen;
    
    COMMAND_PATH    eCmdPath;           /* UART_REQUEST 전용: COMMAND_PATH */
    unsigned char   uchUartData[2048];
    unsigned int    uiUartDataLen;
    
    unsigned int    uiReqSeq;           /* ENGINE_WORKER_RESPONSE 전용 */
    int             iProtoErrCode;      /* PROTOCOL_ERROR 전용 */
} ACU_ACTION;

typedef struct {
    ACU_ACTION  stAcuAction[8];
    int         iCount;
} ACU_ACTION_LIST;

void acuCoreInit(ACU_CTRL_CTX* pstAcuCtrlCtx);

/* UDS에서 “프레임 디코드 성공 + cmdDispatch 성공”까지 끝난 후
 * cmd/reqId/cmdData를 core에 전달한다.
 */
void acuCoreOnUdsCommand(ACU_CTRL_CTX* pstAcuCtrlCtx, unsigned short unCmd,
                          unsigned int uiReqId, const void* pvCmdData, unsigned int uiCmdDataLen,
                          ACU_ACTION_LIST* pstOutAcuActionList);

/* UART에서 바이트를 받았을 때 core에 전달 */
void acuCoreOnUartRx(ACU_CTRL_CTX* pstAcuCtrlCtx,
                      const unsigned char* uchUartBytes, unsigned int uiUartLen,
                      ACU_ACTION_LIST* pstOutAcuActionList);
