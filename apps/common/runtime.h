#ifndef RUNTIME_H
#define RUNTIME_H

#include <event2/event.h>
#include "eventEngine.h"

/* 종료 상태를 명확히 관리하는 컨텍스트 */
typedef struct {
    EVENT_ENGINE *pstEventEngine;
    int           iShuttingDown;
    const char   *pchTag;
    struct event *pstSigEvent;
} APP_SIGNAL_HANDLE;

/* SIGINT 등록/해제 */
APP_SIGNAL_HANDLE* appSignalCreate(EVENT_ENGINE *pstEventEngine, const char *pchTag);
void appSignalDestroy(APP_SIGNAL_HANDLE **ppstAppSignalHandle);

#endif
