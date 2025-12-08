#ifndef TX_QUEUE_H
#define TX_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 전방 선언 */
struct _EVENT_SOURCE;
typedef struct _EVENT_SOURCE EVENT_SOURCE;

/**
 * @brief 단일 송신 노드
 */
typedef struct _TX_NODE {
    EVENT_SOURCE* src;
    int           len;
    unsigned char data[4096];

    struct _TX_NODE* next;
} TX_NODE;

/**
 * @brief 송신 큐
 */
typedef struct _TX_QUEUE {
    TX_NODE* head;
    TX_NODE* tail;
} TX_QUEUE;

void txQueueInit(TX_QUEUE* q);
void txQueueClear(TX_QUEUE* q);

/**
 * @brief 송신 노드 push
 */
void txQueuePush(TX_QUEUE* q, EVENT_SOURCE* src,
                 const unsigned char* data, int len);

/**
 * @brief 한 노드 pop (없으면 -1 리턴)
 */
int txQueuePop(TX_QUEUE* q, EVENT_SOURCE** pSrc,
               unsigned char* buf, int bufSize);

#ifdef __cplusplus
}
#endif

#endif /* TX_QUEUE_H */
