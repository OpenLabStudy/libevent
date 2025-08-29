// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ===== Versioning ===== */
#define PROTO_VER 0x0001

/* ===== Constants ===== */
#define STX_CONST 0xAA55
#define ETX_CONST 0x55AA

/* ===== Commands ===== */
enum {
    CMD_REQ_ID      = 1,
    CMD_KEEP_ALIVE  = 2,
    CMD_IBIT        = 3
};

/* ===== Packing portability ===== */
#define PACKED __attribute__((__packed__))

/* ===== Message types ===== */
typedef struct PACKED {
    unsigned char   uchSrcId;
    unsigned char   uchDstId;
} MSG_ID;

typedef struct PACKED {
    unsigned short  unStx;      /* STX (network order on wire) */
    int             iDataLength;    /* payload length (bytes)      */
    MSG_ID          stMsgId;    /* src/dst IDs                 */
    unsigned char   uchSubModule;
    unsigned short  unCmd;       /* command id                  */
} FRAME_HEADER;

typedef struct PACKED {
    unsigned char   uchCrc;      /* XOR of payload              */
    unsigned short  unEtx;      /* ETX                         */
} FRAME_TAIL;

/* REQ_ID */
typedef struct PACKED { char chTmp;     } REQ_ID;
typedef struct PACKED { char chResult;  } RES_ID;

/* KEEP_ALIVE */
typedef struct PACKED { char chTmp;     } REQ_KEEP_ALIVE;
typedef struct PACKED { char chResult;  } RES_KEEP_ALIVE;

/* IBIT */
typedef struct PACKED { char chIbit;            } REQ_IBIT;
typedef struct PACKED { char chBitTotResult; char chPositionResult; } RES_IBIT;

#if defined(_MSC_VER)
  #pragma pack(pop)
#endif

/* ===== Compile-time layout checks (C11 이상이면 권장) ===== */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  _Static_assert(sizeof(MSG_ID)      == 2,  "MSG_ID must be 2 bytes");
  _Static_assert(sizeof(FRAME_HEADER)== 11, "Check packing of FRAME_HEADER");
  _Static_assert(sizeof(FRAME_TAIL)  == 3,  "FRAME_TAIL must be 3 bytes");
#endif

/* ===== Helpers (inline) ===== */
static inline uint8_t proto_crc8_xor(const uint8_t* p, size_t n) {
    uint8_t c = 0; for (size_t i=0;i<n;i++) c ^= p[i]; return c;
}

/* host<->network (16/32) : <arpa/inet.h>에서 제공.
   다만 여기선 선언만. 구현 파일에서 htons/htonl/ntohs/ntohl 사용하세요. */

#endif /* PROTOCOL_H */
