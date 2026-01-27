#ifndef IO_CHANNEL_UTIL_H
#define IO_CHANNEL_UTIL_H

#include "eventEngine.h"

/* SIGPIPE 보호 */
void ioIgnoreSigpipeOnce(void);

/* IO_CHANNEL 상태 */
int  ioIsChannelAlive(const IO_CHANNEL* pstIoChannel);
void ioMarkChannelDead(IO_CHANNEL* pstIoChannel, IO_EVENT_TYPE eEventType);

/* IO_CHANNEL 탐색 */
IO_CHANNEL* ioFindChannelByWorkerId(EVENT_ENGINE* pstEngine, int iWorkerId);

#endif
