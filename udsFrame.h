#ifndef __UDS_FRAME_H__
#define __UDS_FRAME_H__

#include <stddef.h>

/* ========================================================================== */
/*  Constants                                                                 */
/* ========================================================================== */

#define UDS_STX_CONST   0xA55A
#define UDS_ETX_CONST   0x5AA5
#define UDS_MAX_SIZE    2048

#define UDS_FRAME_HEADER_SIZE   (2 + 4 + 4)   /* STX + DataLen + RequestID */
#define UDS_FRAME_TAIL_SIZE     (2)           /* ETX */
#define UDS_FRAME_MIN_SIZE      (UDS_FRAME_HEADER_SIZE + UDS_FRAME_TAIL_SIZE)

/* ========================================================================== */
/*  Error Code                                                                */
/* ========================================================================== */

typedef enum {
    UDS_FRAME_OK = 0,
    UDS_FRAME_NEED_MORE_DATA,
    UDS_FRAME_INVALID_STX,
    UDS_FRAME_INVALID_ETX,
    UDS_FRAME_INVALID_LENGTH,
    UDS_FRAME_NULL_PTR
} UDS_FRAME_ERR;

/* ========================================================================== */
/*  Header Structure                                                          */
/* ========================================================================== */

#pragma pack(push, 1)
typedef struct {
    unsigned short unStx;        /* 2 bytes */
    unsigned int   uiDataLen;    /* 4 bytes */
    unsigned int   uiRequestId;  /* 4 bytes */
} UDS_FRAME_HEADER;
#pragma pack(pop)

/* ========================================================================== */
/*  Encode API                                                                */
/* ========================================================================== */

UDS_FRAME_ERR udsFrameBuildRequest(
    unsigned int uiRequestId,
    const unsigned char *puchPayload,
    unsigned int uiPayloadLen,
    unsigned char *puchOutBuf,
    unsigned int uiOutBufSize,
    unsigned int *puiOutFrameSize);

/* ========================================================================== */
/*  Decode API                                                                */
/* ========================================================================== */

UDS_FRAME_ERR udsFrameDecode(
    const unsigned char *puchBuf,
    unsigned int uiBufLen,
    unsigned int *puiOutRequestId,
    const unsigned char **ppuchOutPayload,
    unsigned int *puiOutPayloadLen);

/* ========================================================================== */
/*  Utility                                                                   */
/* ========================================================================== */

int udsFrameGetTotalSize(const unsigned char *puchBuf,
                         unsigned int uiBufLen);

const char* udsFrameErrToStr(UDS_FRAME_ERR eErr);

#endif /* __UDS_FRAME_H__ */
