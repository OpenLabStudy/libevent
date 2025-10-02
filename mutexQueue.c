/*
 * mutexQueue.c
 * Mutex + Condition Variable 기반 고정 크기 원형 큐 구현
 */

#include "mutexQueue.h"
#include <stdlib.h>
#include <errno.h>
#include <time.h>

MUTEX_QUEUE* newMutexQueue(size_t ulMaxSize)
{
    MUTEX_QUEUE* pstMutexQueue = (MUTEX_QUEUE*)calloc(1, sizeof(MUTEX_QUEUE));
    if (!pstMutexQueue)
        return NULL;

    pstMutexQueue->ppvBuffer = (void**)calloc(ulMaxSize, sizeof(void*));
    if (!pstMutexQueue->ppvBuffer) { 
        free(pstMutexQueue);
        return NULL;
    }

    pstMutexQueue->ulMaxSize = ulMaxSize;
    pthread_mutex_init(&pstMutexQueue->uniMutex, NULL);
    pthread_cond_init(&pstMutexQueue->uniNotEmpty, NULL);
    pthread_cond_init(&pstMutexQueue->uniNotFull, NULL);
    return pstMutexQueue;
}

void freeMutexQueue(MUTEX_QUEUE* pstMutexQueue)
{
    if (!pstMutexQueue)
        return;

    pthread_mutex_destroy(&pstMutexQueue->uniMutex);
    pthread_cond_destroy(&pstMutexQueue->uniNotEmpty);
    pthread_cond_destroy(&pstMutexQueue->uniNotFull);
    free(pstMutexQueue->ppvBuffer);
    free(pstMutexQueue);
}

char pushMutexQueueNoWait(MUTEX_QUEUE* pstMutexQueue, void* pvData)
{
    char chRetVal = 0x01;
    pthread_mutex_lock(&pstMutexQueue->uniMutex);
    if (pstMutexQueue->ulCnt == pstMutexQueue->ulMaxSize) { // full
        chRetVal = 0x00;
    } else {
        pstMutexQueue->ppvBuffer[pstMutexQueue->ulTail] = pvData;
        pstMutexQueue->ulTail = (pstMutexQueue->ulTail + 1) % pstMutexQueue->ulMaxSize;
        pstMutexQueue->ulCnt++;
        pthread_cond_signal(&pstMutexQueue->uniNotEmpty);
    }
    pthread_mutex_unlock(&pstMutexQueue->uniMutex);
    return chRetVal;
}

void pushMutexQueueWait(MUTEX_QUEUE* pstMutexQueue, void* pvData) {
    pthread_mutex_lock(&pstMutexQueue->uniMutex);
    while (pstMutexQueue->ulCnt == pstMutexQueue->ulMaxSize){
        pthread_cond_wait(&pstMutexQueue->uniNotFull, &pstMutexQueue->uniMutex);
    }

    pstMutexQueue->ppvBuffer[pstMutexQueue->ulTail] = pvData;
    pstMutexQueue->ulTail = (pstMutexQueue->ulTail + 1) % pstMutexQueue->ulMaxSize;
    pstMutexQueue->ulCnt++;
    pthread_cond_signal(&pstMutexQueue->uniNotEmpty);
    pthread_mutex_unlock(&pstMutexQueue->uniMutex);
}

void* popMutexQueueWaitTimeout(MUTEX_QUEUE* pstMutexQueue, int iTimeoutMsec) {
    void* ptr = NULL;
    pthread_mutex_lock(&pstMutexQueue->uniMutex);

    if (iTimeoutMsec < 0) {
        while (pstMutexQueue->ulCnt == 0){
            pthread_cond_wait(&pstMutexQueue->uniNotEmpty, &pstMutexQueue->uniMutex);
        }
    } else if (iTimeoutMsec > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += iTimeoutMsec / 1000;
        ts.tv_nsec += (iTimeoutMsec % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        while (pstMutexQueue->ulCnt == 0) {
            int rc = pthread_cond_timedwait(&pstMutexQueue->uniNotEmpty, &pstMutexQueue->uniMutex, &ts);
            if (rc == ETIMEDOUT) break;
        }
    } // else iTimeoutMsec == 0 → 즉시 확인

    if (pstMutexQueue->ulCnt > 0) {
        ptr = pstMutexQueue->ppvBuffer[pstMutexQueue->ulHead];
        pstMutexQueue->ulHead = (pstMutexQueue->ulHead + 1) % pstMutexQueue->ulMaxSize;
        pstMutexQueue->ulCnt--;
        pthread_cond_signal(&pstMutexQueue->uniNotFull);
    }

    pthread_mutex_unlock(&pstMutexQueue->uniMutex);
    return ptr;
}
