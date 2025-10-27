#ifndef ICD_COMMAND_H
#define ICD_COMMAND_H

#include <stdint.h>
#include <stddef.h>


/* ===== Commands ===== */
enum {
    CMD_REQ_ID      = 1,
    CMD_KEEP_ALIVE  = 2,
    CMD_IBIT        = 3
};

/* ===== Packing portability ===== */
#define PACKED __attribute__((__packed__))

/* REQ_ID */
typedef struct PACKED { char chTmp;     } REQ_ID;
typedef struct PACKED { char chResult;  } RES_ID;

/* KEEP_ALIVE */
typedef struct PACKED { char chTmp;     } REQ_KEEP_ALIVE;
typedef struct PACKED { char chResult;  } RES_KEEP_ALIVE;

/* IBIT */
typedef struct PACKED { char chIbit;            } REQ_IBIT;
typedef struct PACKED { char chBitTotResult; char chPositionResult; } RES_IBIT;


#endif /* ICD_COMMAND_H */
