#include <stdio.h>
#include <string.h>

#include "internal.h"

#include "acuUtil.h"
#include "icdCommand.h"
#include "cmdRegistry.h"

/* Context init (원본 유지) */
void initAcuCtrlCtx(ACU_CTRL_CTX* pstAcuCtrlCtx)
{
    pstAcuCtrlCtx->eAcuState                    = ACU_STATE_IDLE;
    pstAcuCtrlCtx->uiLocalReqId                 = 1;
    pstAcuCtrlCtx->uiReqId                      = 1;
    pstAcuCtrlCtx->unCmd                        = CMD_UNKNOWN;
    pstAcuCtrlCtx->stCommandState.chAcuMode     = ACU_MODE_NONE;
    pstAcuCtrlCtx->stCommandState.chSendOnOff   = AZ_EL_SEND_OFF;
    pstAcuCtrlCtx->stCommandState.dAzOffset     = 0.0;
    pstAcuCtrlCtx->stCommandState.dElOffset     = 0.0;

    /* 헤더에 있는 확장 필드(동작 영향 없음) */
    pstAcuCtrlCtx->stPending.chInUse = 0;
    pstAcuCtrlCtx->stPending.unCmd = CMD_UNKNOWN;
    pstAcuCtrlCtx->stPending.uiReqId = 0;
    pstAcuCtrlCtx->stPending.pstUdsIo = NULL;
    pstAcuCtrlCtx->pstTimeoutEvt = NULL;
    pstAcuCtrlCtx->chIsUartAlive = 1;
    pstAcuCtrlCtx->chIsSendCommand = 0;
    memset(&pstAcuCtrlCtx->stLastAzElRxTime, 0, sizeof(pstAcuCtrlCtx->stLastAzElRxTime));
}

COMMAND_PATH decideProcessingPath(unsigned short unCmd)
{
    switch(unCmd)
    {
        case CMD_ID_INFO:
            return COMMAND_PATH_NONE;
        case CMD_POSITIONER_AZ_EL_SET:
        case CMD_ACU_MODE_SELECT:
        case CMD_POSITIONER_AZ_EL:
            return ACU_CTRL_UART;
        default:
            return COMMAND_PATH_FAIL;
    }
}

void createAcuUartData(ACU_CTRL_CTX* pstAcuCtrlCtx, unsigned short unCmd,
                       char* pchCmdData, char* pchOut, unsigned int* pOutLen)
{
    switch(unCmd)
    {
        case CMD_POSITIONER_AZ_EL:
            *pOutLen = readAzElFromAcu(pchOut);
        break;

        case CMD_POSITIONER_AZ_EL_SET:
        {
            REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet = (REQ_POSITIONER_AZ_EL_SET *)pchCmdData;
            double dAz = endianChange(pstReqAzElSet->chAzimuthDeg);
            double dEl = endianChange(pstReqAzElSet->chElevationDeg);

            fprintf(stderr, "%s():%d ACU AZ/EL Set to AZ: %.2f, EL: %.2f\n",
                    __func__,__LINE__, dAz, dEl);

            if(pstAcuCtrlCtx->stCommandState.chAcuMode == POSITION){
                *pOutLen = moveAzElPosition(dAz, dEl, pchOut);
            }
            break;
        }

        case CMD_ACU_MODE_SELECT:
        {
            REQ_ACU_MODE *pstReqAcuMode = (REQ_ACU_MODE*)pchCmdData;
            fprintf(stderr, "\nACU Mode Change to %s\n",
                    pstReqAcuMode->chAcuMode == POSITION ? "POSITION MODE" : "RATE MODE");

            pstAcuCtrlCtx->stCommandState.chAcuMode = pstReqAcuMode->chAcuMode;
            *pOutLen = modeChange(pstReqAcuMode->chAcuMode, pchOut);
            break;
        }

        case CMD_ID_INFO:
        {
            RES_ID *pstResId = (RES_ID *)(pchCmdData);
            pstResId->chId = (char)AC_RCV_CMD_FROM_TC;
            fprintf(stderr, "RES_ID %04X\n", pstResId->chId);
            *pOutLen = sizeof(RES_ID);
        }
        break;

        default:
        break;
    }
}
