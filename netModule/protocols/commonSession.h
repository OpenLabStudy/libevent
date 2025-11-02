#ifndef COMMON_SESSION_H
#define COMMON_SESSION_H

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <stdint.h>

/* 연결별 컨텍스트 */
typedef struct session_ctx  SESSION_CTX;
typedef struct core_ctx     CORE_CTX;

struct core_ctx {
    struct event_base   *pstEventBase;    
    struct event        *pstAcceptEvent;
    struct event        *pstSignalEvent;
    int                 iClientCount;
    int                 iClientSock;
    SESSION_CTX         *pstSockCtxHead;
};

struct session_ctx {
    struct bufferevent  *pstBufferEvent;
    CORE_CTX            *pstCoreCtx;
    unsigned short      unCmd;
    int                 iDataLength;
    unsigned char       uchSrcId;
    unsigned char       uchDstId;
    unsigned char       uchIsResponse;    
    SESSION_CTX         *pstSockCtxNext;
};

void sessionInitCore(CORE_CTX* pstCoreCtx, struct event_base* pstEventBase);
void sessionReadCallback(struct bufferevent* pstBufferEvent, void* pvData);
void sessionEventCallback(struct bufferevent* pstBufferEvent, short nEvents, void* pvData);
void sessionCloseAndFree(void* pvData);

/* 연결 리스트 관리 */
void sessionAdd(SESSION_CTX *pstSessionCtx, CORE_CTX *pstCoreCtx);
void sessionRemove(SESSION_CTX *pstSessionCtx);

#endif
