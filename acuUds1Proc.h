#ifndef ACU_UDS1_PROC_H
#define ACU_UDS1_PROC_H

#include "eventEngine.h"
#include "ipcUtil.h"

void commandEventCb(int iFd, short nEvent, void *pvData);
int  acuSendUartAndPend(ACU_CTRL_CTX* pstCtx, const IPC_CMD_CTX* pstCmdCtx,
                        IO_CHANNEL* pstUdsIo, unsigned int uiReqId);
void acuUartTimeoutCb(evutil_socket_t fd, short what, void* arg);

#endif
