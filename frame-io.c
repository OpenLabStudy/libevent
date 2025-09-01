#include "frame.h"
#include "icdCommand.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <event2/buffer.h>

/* ===== Helpers (inline) ===== */
static inline uint8_t proto_crc8_xor(const uint8_t* p, size_t n) {
    uint8_t c = 0; for (size_t i=0;i<n;i++) c ^= p[i]; return c;
}

/* === 프레임 송신 === */
void writeFrame(struct bufferevent* pstBufferEvent, unsigned short unCmd,
                       const MSG_ID* pstMsgId, unsigned char uchSubModule,
                       const void* pvPayload, int iDataLength)
{
    FRAME_HEADER stFrameHeader;
    FRAME_TAIL   stFrameTail;

    stFrameHeader.unStx             = htons(STX_CONST);
    stFrameHeader.iDataLength       = htonl(iDataLength);
    stFrameHeader.stMsgId.uchSrcId  = pstMsgId->uchSrcId;
    stFrameHeader.stMsgId.uchDstId  = pstMsgId->uchDstId;
    stFrameHeader.uchSubModule      = uchSubModule;
    stFrameHeader.unCmd             = htons(unCmd);

    stFrameTail.uchCrc              = proto_crc8_xor((const unsigned char*)pvPayload, (int)iDataLength);
    stFrameTail.unEtx               = htons(ETX_CONST);

    bufferevent_write(pstBufferEvent, &stFrameHeader, sizeof(stFrameHeader));
    if (iDataLength > 0 && pvPayload)
        bufferevent_write(pstBufferEvent, pvPayload, iDataLength);
    bufferevent_write(pstBufferEvent, &stFrameTail, sizeof(stFrameTail));
}


/* === 프레임 하나 파싱 & 응답 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
int responseFrame(struct evbuffer* pstEvBuffer, struct bufferevent  *pstBufferEvent, MSG_ID* pstMsgId, char chReply) {    
    int iLength = evbuffer_get_length(pstEvBuffer);
    fprintf(stderr,"### %s():%d Recv Data Length is %d[%d]\n", __func__, __LINE__, iLength, sizeof(FRAME_HEADER)+sizeof(FRAME_TAIL)+sizeof(REQ_KEEP_ALIVE));
    if (iLength < sizeof(FRAME_HEADER))
        return 0;

    FRAME_HEADER stFrameHeader;
    if (evbuffer_copyout(pstEvBuffer, &stFrameHeader, sizeof(stFrameHeader)) != sizeof(stFrameHeader))
        return 0;

    unsigned short  unStx   = ntohs(stFrameHeader.unStx);
    int iDataLength = ntohl(stFrameHeader.iDataLength);
    unsigned short  unCmd  = ntohs(stFrameHeader.unCmd);
    MSG_ID stMsgId;
    stMsgId.uchSrcId = pstMsgId->uchSrcId;
    stMsgId.uchDstId = stFrameHeader.stMsgId.uchSrcId;

    if (unStx != STX_CONST || iDataLength < 0)
        return -1;

    int iNeedSize = sizeof(FRAME_HEADER) + (size_t)iDataLength + sizeof(FRAME_TAIL);
    if (iLength < iNeedSize)
        return 0;

    evbuffer_drain(pstEvBuffer, sizeof(FRAME_HEADER));

    unsigned char *uchPayload = NULL;
    if (iDataLength > 0) {
        uchPayload = (unsigned char *)malloc((size_t)iDataLength);
        if (!uchPayload) 
            return -1;
        if (evbuffer_remove(pstEvBuffer, uchPayload, (size_t)iDataLength) != iDataLength) {
            free(uchPayload); 
            return -1;
        }
    }

    FRAME_TAIL stFrameTail;
    if (evbuffer_remove(pstEvBuffer, &stFrameTail, sizeof(stFrameTail)) != (int)sizeof(stFrameTail)) {
        free(uchPayload);
        return -1;
    }

    if (ntohs(stFrameTail.unEtx) != ETX_CONST ||
        proto_crc8_xor(uchPayload, (size_t)iDataLength) != (unsigned char)stFrameTail.uchCrc) {
        free(uchPayload); 
        return -1;
    }

    /* === 응답 처리 ===
       - 기본 가정: 요청 CMD와 응답 CMD가 동일
    */
    switch (unCmd) {
        case CMD_REQ_ID: {
            RES_ID stResId;
            stResId.chResult = pstMsgId->uchSrcId;
            fprintf(stderr, "RES_ID : result=%d, len:%d\n", stResId.chResult, iDataLength);
            if(chReply)
                writeFrame(pstBufferEvent, CMD_REQ_ID, pstMsgId, 0, &stResId, sizeof(RES_ID));

            break;
        }
        case CMD_KEEP_ALIVE: {
            RES_KEEP_ALIVE stResKeepAlive;
            stResKeepAlive.chResult = 0x01;
            fprintf(stderr, "RES_KEEP_ALIVE : result=%d, len:%d\n", stResKeepAlive.chResult, iDataLength);
            if(chReply)
                writeFrame(pstBufferEvent, CMD_KEEP_ALIVE, pstMsgId, 0, &stResKeepAlive, sizeof(RES_KEEP_ALIVE));
            break;
        }
        case CMD_IBIT: {
            RES_IBIT stResIbit;
            stResIbit.chBitTotResult = 0x01;
            stResIbit.chPositionResult = 0x01;
            fprintf(stderr, "IBIT : total=%d position=%d\n", stResIbit.chBitTotResult, stResIbit.chPositionResult);
            if(chReply)
                writeFrame(pstBufferEvent, CMD_IBIT, pstMsgId, 0, &stResIbit, sizeof(RES_IBIT));
            break;
        }
        default:
            fprintf(stderr, "RES cmd=%d len=%d\n", unCmd, iDataLength);
            break;
    }
    free(uchPayload);
    iLength = evbuffer_get_length(pstEvBuffer);
    fprintf(stderr,"### %s():%d Recv Data Length is %d\n", __func__, __LINE__, iLength);
    return 1;
}


/* === 프레임 하나 파싱 & 응답 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
int requestFrame(struct bufferevent  *pstBufferEvent, MSG_ID* pstMsgId, unsigned short unCmd) {
    switch (unCmd) {
        case CMD_REQ_ID: {
            REQ_ID stReqId;
            stReqId.chTmp = 0x01;
            fprintf(stderr, "REQ_ID\n");
            writeFrame(pstBufferEvent, CMD_REQ_ID, pstMsgId, 0, &stReqId, sizeof(REQ_ID));
            break;
        }
        case CMD_KEEP_ALIVE: {
            REQ_KEEP_ALIVE stReqKeepAlive;
            stReqKeepAlive.chTmp = 0x01;
            fprintf(stderr, "REQ_KEEP_ALIVE\n");
            writeFrame(pstBufferEvent, CMD_REQ_ID, pstMsgId, 0, &stReqKeepAlive, sizeof(REQ_KEEP_ALIVE));
            break;
        }
        case CMD_IBIT: {
            REQ_IBIT stReqIbit;
            stReqIbit.chIbit = 0x01;
            fprintf(stderr, "REQ_IBIT\n");
            writeFrame(pstBufferEvent, CMD_REQ_ID, pstMsgId, 0, &stReqIbit, sizeof(RES_IBIT));
            break;
        }
        default:
            fprintf(stderr, "REQ CMD=%d\n", unCmd);
            break;
    }
    return 1;
}