#ifndef IPC_UTIL_H
#define IPC_UTIL_H

#include "icdCommand.h"
#include "frame.h"
#include "netUds.h"
#include "netCore.h"
#include "ioChannelUtil.h"

/* Worker ID → MSG_ID 변환 */
int ipcBuildMsgIdFromWorker(int iWorkerId, void* pvMsgId);

/* Worker Register 전송 */
void ipcSendWorkerRegister(IO_CHANNEL* pstIoChannel, unsigned char uchWorkerType);

#endif
