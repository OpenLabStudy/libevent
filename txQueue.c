/* txQueue.c */

#include "txQueue.h"
#include <stdlib.h>

void txQueuePush(TX_QUEUE* q, TX_ITEM* item)
{
    item->pstNext = NULL;
    if (!q->pstTail) {
        q->pstHead = q->pstTail = item;
    } else {
        q->pstTail->pstNext = item;
        q->pstTail = item;
    }
}

TX_ITEM* txQueuePop(TX_QUEUE* q)
{
    TX_ITEM* item = q->pstHead;
    if (!item)
        return NULL;

    q->pstHead = item->pstNext;
    if (!q->pstHead)
        q->pstTail = NULL;

    item->pstNext = NULL;
    return item;
}
