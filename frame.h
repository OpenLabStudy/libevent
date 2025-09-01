#ifndef FRAME_H
#define FRAME_H

#include <event2/event.h>
#include <event2/bufferevent.h>

/* ===== Constants ===== */
#define STX_CONST 0xAA55
#define ETX_CONST 0x55AA


/* ===== Message types ===== */
typedef struct __attribute__((__packed__)) {
    unsigned char   uchSrcId;
    unsigned char   uchDstId;
} MSG_ID;

typedef struct __attribute__((__packed__)) {
    unsigned short  unStx;      /* STX (network order on wire) */
    int             iDataLength;    /* payload length (bytes)      */
    MSG_ID          stMsgId;    /* src/dst IDs                 */
    unsigned char   uchSubModule;
    unsigned short  unCmd;       /* command id                  */
} FRAME_HEADER;

typedef struct __attribute__((__packed__)) {
    unsigned char   uchCrc;      /* XOR of payload              */
    unsigned short  unEtx;      /* ETX                         */
} FRAME_TAIL;



void writeFrame(struct bufferevent* pstBufferEvent, unsigned short unCmd,
                       const MSG_ID* pstMsgId, unsigned char uchSubModule,
                       const void* pvPayload, int iDataLength);
int responseFrame(struct evbuffer* pstEvBuffer, struct bufferevent  *pstBufferEvent, MSG_ID* pstMsgId, char chReply);
int requestFrame(struct bufferevent  *pstBufferEvent, MSG_ID* pstMsgId, unsigned short unCmd);
#endif
