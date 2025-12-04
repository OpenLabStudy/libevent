/* txQueue.h */

#ifndef TX_QUEUE_H
#define TX_QUEUE_H

#include "eventSession.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TX_TO_TCP_ONE,
    TX_TO_TCP_ALL,
    TX_TO_UDS_ONE,
    TX_TO_UDS_ALL
} TX_DEST;

typedef struct _TX_ITEM
{
    TX_DEST         eDest;
    SOCK_CONTEXT*   pstTarget;          /**< TX_TO_*_ONE 일 때 대상 */
    size_t          tLen;
    unsigned char   auchBuf[2048];

    struct _TX_ITEM* pstNext;
} TX_ITEM;

typedef struct _TX_QUEUE
{
    TX_ITEM* pstHead;
    TX_ITEM* pstTail;
} TX_QUEUE;

static inline void txQueueInit(TX_QUEUE* q)
{
    q->pstHead = q->pstTail = NULL;
}

void txQueuePush(TX_QUEUE* q, TX_ITEM* item);
TX_ITEM* txQueuePop(TX_QUEUE* q);

#endif /* TX_QUEUE_H */
