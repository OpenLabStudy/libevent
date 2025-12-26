#include "udsFrame.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

/* ========================================================================== */
/*  Encode                                                                    */
/* ========================================================================== */

UDS_FRAME_ERR udsFrameBuildRequest(
    unsigned int uiRequestId,
    const unsigned char *puchPayload,
    unsigned int uiPayloadLen,
    unsigned char *puchOutBuf,
    unsigned int uiOutBufSize,
    unsigned int *puiOutFrameSize)
{
    if (!puchOutBuf || !puiOutFrameSize)
        return UDS_FRAME_NULL_PTR;

    unsigned int uiTotalSize =
        UDS_FRAME_HEADER_SIZE + uiPayloadLen + UDS_FRAME_TAIL_SIZE;

    if (uiOutBufSize < uiTotalSize)
        return UDS_FRAME_INVALID_LENGTH;

    UDS_FRAME_HEADER *pstHeader =
        (UDS_FRAME_HEADER *)puchOutBuf;

    pstHeader->unStx        = htons(UDS_STX_CONST);
    pstHeader->uiDataLen    = htonl(uiPayloadLen);
    pstHeader->uiRequestId  = htonl(uiRequestId);

    if (uiPayloadLen > 0 && puchPayload)
        memcpy(puchOutBuf + UDS_FRAME_HEADER_SIZE,
               puchPayload,
               uiPayloadLen);

    unsigned short *punEtx =
        (unsigned short *)(puchOutBuf
            + UDS_FRAME_HEADER_SIZE
            + uiPayloadLen);

    *punEtx = htons(UDS_ETX_CONST);

    *puiOutFrameSize = uiTotalSize;
    return UDS_FRAME_OK;
}

/* ========================================================================== */
/*  Decode                                                                    */
/* ========================================================================== */

UDS_FRAME_ERR udsFrameDecode(
    const unsigned char *puchBuf,
    unsigned int uiBufLen,
    unsigned int *puiOutRequestId,
    const unsigned char **ppuchOutPayload,
    unsigned int *puiOutPayloadLen)
{
    if (!puchBuf || !puiOutRequestId ||
        !ppuchOutPayload || !puiOutPayloadLen)
        return UDS_FRAME_NULL_PTR;

    if (uiBufLen < UDS_FRAME_MIN_SIZE)
        return UDS_FRAME_NEED_MORE_DATA;

    const UDS_FRAME_HEADER *pstHeader =
        (const UDS_FRAME_HEADER *)puchBuf;
        
    if (ntohs(pstHeader->unStx) != UDS_STX_CONST)
        return UDS_FRAME_INVALID_STX;
        
    unsigned int uiPayloadLen = ntohl(pstHeader->uiDataLen);
    unsigned int uiTotalSize = UDS_FRAME_HEADER_SIZE + uiPayloadLen + UDS_FRAME_TAIL_SIZE;

    if (uiBufLen < uiTotalSize)
        return UDS_FRAME_NEED_MORE_DATA;
        
    const unsigned short *punEtx =
        (const unsigned short *)(puchBuf
            + UDS_FRAME_HEADER_SIZE
            + uiPayloadLen);
            
    if (ntohs(*punEtx) != UDS_ETX_CONST)
        return UDS_FRAME_INVALID_ETX;
        
    *puiOutRequestId  = ntohl(pstHeader->uiRequestId);
    *ppuchOutPayload  = puchBuf + UDS_FRAME_HEADER_SIZE;
    *puiOutPayloadLen = uiPayloadLen;

    return UDS_FRAME_OK;
}

/* ========================================================================== */
/*  Utility                                                                   */
/* ========================================================================== */

int udsFrameGetTotalSize(const unsigned char *puchBuf,
                         unsigned int uiBufLen)
{
    if (!puchBuf || uiBufLen < UDS_FRAME_HEADER_SIZE)
        return -1;

    const UDS_FRAME_HEADER *pstHeader =
        (const UDS_FRAME_HEADER *)puchBuf;

    if (ntohs(pstHeader->unStx) != UDS_STX_CONST)
        return -1;

    unsigned int uiPayloadLen = ntohl(pstHeader->uiDataLen);

    return (int)(UDS_FRAME_HEADER_SIZE
                 + uiPayloadLen
                 + UDS_FRAME_TAIL_SIZE);
}

const char* udsFrameErrToStr(UDS_FRAME_ERR eErr)
{
    switch (eErr) {
        case UDS_FRAME_OK:             return "UDS_FRAME_OK";
        case UDS_FRAME_NEED_MORE_DATA: return "UDS_FRAME_NEED_MORE_DATA";
        case UDS_FRAME_INVALID_STX:    return "UDS_FRAME_INVALID_STX";
        case UDS_FRAME_INVALID_ETX:    return "UDS_FRAME_INVALID_ETX";
        case UDS_FRAME_INVALID_LENGTH: return "UDS_FRAME_INVALID_LENGTH";
        case UDS_FRAME_NULL_PTR:       return "UDS_FRAME_NULL_PTR";
        default:                       return "UDS_FRAME_UNKNOWN";
    }
}
