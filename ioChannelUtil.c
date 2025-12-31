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
    fprintf(stderr,"### %s():%d %u ###\n",__func__,__LINE__, pstIoChannel);
    if (!pstIoChannel) 
        return 0;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (pstIoChannel->chFdCloseSet == 0x01) 
        return 0;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    if (pstIoChannel->iFd < 0) 
        return 0;
        fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    return 1;
}

void ioMarkChannelDead(IO_CHANNEL* pstIoChannel, IO_EVENT_TYPE eEventType)
{
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
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
    IO_CHANNEL* pstCur = pstEngine->pstIoChannelList;
    while (pstCur) {
        fprintf(stderr,"### %s():%d %d-%d###\n",__func__,__LINE__, iWorkerId, pstCur->iWorkerId);
        if (pstCur->iWorkerId == iWorkerId)
            return pstCur;
        pstCur = pstCur->pstNextIoChannel;
    }
    return NULL;
}
