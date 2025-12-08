#include "txQueue.h"
#include <stdlib.h>
#include <string.h>

void txQueueInit(TX_QUEUE* q)
{
    q->head = q->tail = NULL;
}

void txQueueClear(TX_QUEUE* q)
{
    TX_NODE* n = q->head;
    while (n) {
        TX_NODE* next = n->next;
        free(n);
        n = next;
    }
    q->head = q->tail = NULL;
}

void txQueuePush(TX_QUEUE* q, EVENT_SOURCE* src,
                 const unsigned char* data, int len)
{
    if (len <= 0 || len > (int)sizeof(((TX_NODE*)0)->data))
        return;

    TX_NODE* n = (TX_NODE*)calloc(1, sizeof(TX_NODE));
    if (!n) return;

    n->src = src;
    n->len = len;
    memcpy(n->data, data, len);

    if (!q->head) {
        q->head = q->tail = n;
    } else {
        q->tail->next = n;
        q->tail = n;
    }
}

int txQueuePop(TX_QUEUE* q, EVENT_SOURCE** pSrc,
               unsigned char* buf, int bufSize)
{
    if (!q->head) return -1;

    TX_NODE* n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;

    if (pSrc) *pSrc = n->src;

    int len = n->len;
    if (buf && bufSize > 0) {
        if (len > bufSize) len = bufSize;
        memcpy(buf, n->data, len);
    }

    free(n);
    return len;
}
