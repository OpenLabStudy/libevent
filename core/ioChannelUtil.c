#include "ioChannelUtil.h"
#include <signal.h>
#include <unistd.h>

/* UDS 서버가 먼저 죽었을 때 write()가 SIGPIPE로 프로세스를 죽이는 것을 방지 */
void ioIgnoreSigpipeOnce(void)
{
    static int s_inited = 0;
    if (!s_inited) {
        signal(SIGPIPE, SIG_IGN);
        s_inited = 1;
    }
}

int ioIsChannelAlive(const IO_CHANNEL* pstIoChannel)
{
    if (!pstIoChannel) 
        return 0; 
    if (pstIoChannel->chFdCloseSet == FD_CLOSED) 
        return 0;
    if (pstIoChannel->iFd < 0) 
        return 0;
    return 1;
}

void ioMarkChannelDead(IO_CHANNEL* pstIoChannel, IO_EVENT_TYPE eEventType)
{
    if (!pstIoChannel)
        return;

    pstIoChannel->chFdCloseSet = 0x01;
    pstIoChannel->ePendingLogicEvent = eEventType;

    if (pstIoChannel->iFd >= 0) {
        close(pstIoChannel->iFd);
        pstIoChannel->iFd = -1;
    }
}

IO_CHANNEL* ioFindChannelByWorkerId(EVENT_ENGINE* pstEngine, int iWorkerId)
{
    if (!pstEngine)
        return NULL;
        
    IO_CHANNEL* pstCur = pstEngine->pstIoChannelList;
    while (pstCur) {
        if (pstCur->iWorkerId == iWorkerId){
            return pstCur;
        }
        pstCur = pstCur->pstNextIoChannel;
    }
    return NULL;
}
