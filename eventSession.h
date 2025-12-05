/**
 * @file eventSession.h
 * @brief Libevent 기반 Session 관리 모듈 (Server / Client 공용, TCP 우선)
 *
 * 이 모듈은 libevent를 이용한 이벤트 루프와, 서버/클라이언트 연결을
 * 역할별 Context로 분리해서 관리하기 위한 공용 헤더이다.
 *
 * - BASE_CONTEXT : 이벤트 루프, 시그널, 콜백 등 공용 리소스
 * - SERVER_CONTEXT : listen 소켓, accept 이벤트, 클라이언트 리스트
 * - SOCK_CONTEXT : 개별 연결(클라이언트) 세션
 *
 * @see eventSession.c
 */

#ifndef EVENTSESSION_H
#define EVENTSESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <stdint.h>

/* ========================================================================== */
/* APP ROLE                                                                   */
/* ========================================================================== */

/**
 * @enum APP_ROLE
 * @brief 애플리케이션의 역할(서버/클라이언트)을 나타내는 열거형
 */
typedef enum {
    ROLE_NONE          = 0x00,
    ROLE_SERVER        = 0x10,

    ROLE_TCP_SERVER    = 0x11, /**< 서버 역할 */
    ROLE_UDP_SERVER    = 0x12, /**< 서버 역할 */
    ROLE_UDS_SERVER    = 0x13, /**< 서버 역할 */

    ROLE_CLIENT        = 0x20,
    ROLE_TCP_CLIENT    = 0x21, /**< 클라이언트 역할 */
    ROLE_UDP_CLIENT    = 0x22, /**< 클라이언트 역할 */
    ROLE_UDS_CLIENT    = 0x23  /**< 클라이언트 역할 */
} APP_ROLE;



/* Forward Declarations */
struct _BASE_CONTEXT;
struct _SERVER_CONTEXT;
struct _SOCK_CONTEXT;

typedef struct _BASE_CONTEXT   BASE_CONTEXT;
typedef struct _SERVER_CONTEXT SERVER_CONTEXT;
typedef struct _SOCK_CONTEXT   SOCK_CONTEXT;


/* ========================================================================== */
/* CALLBACK HANDLER SET                                                       */
/* ========================================================================== */

/* 애플리케이션 콜백 세트 */
typedef void (*READ_CALL_BACK)(struct bufferevent*, void*);
typedef void (*WRITE_CALL_BACK)(struct bufferevent*, void*);
typedef void (*EVENT_CALL_BACK)(struct bufferevent*, short, void*);

typedef struct _EVENT_HANDLER
{
    READ_CALL_BACK  pfReadCb;
    WRITE_CALL_BACK pfWriteCb;
    EVENT_CALL_BACK pfEventCb;
} EVENT_HANDLER;


/* ========================================================================== */
/* BASE CONTEXT (공용 이벤트 루프/시그널/콜백)                                */
/* ========================================================================== */

/**
 * @struct BASE_CONTEXT
 * @brief Libevent 기반 애플리케이션의 공용 Context
 *
 * - event_base, signal event 등 프로세스 공용 리소스
 * - Application 레벨 콜백 세트(EVENT_HANDLER)
 * - 애플리케이션 ID 및 사용자 Context
 */
struct _BASE_CONTEXT
{
    struct event_base*  pstEventBase;   /**< Libevent Base */
    struct event*       pstEvent;       /**< stdin 등 기타 이벤트 (옵션) */
    struct event*       pstSignalEvent; /**< Signal 이벤트 */

    EVENT_HANDLER       stHandler;      /**< 애플리케이션 제공 콜백 */
    void*               pvUserCtx;      /**< 상위 계층에서 사용하는 User Context */

    unsigned char       uchMyId;        /**< 송신자(자기 ID), 프레임 통신 시 활용 */
};


/* ========================================================================== */
/* SERVER CONTEXT (TCP 서버 전용)                                             */
/* ========================================================================== */

/**
 * @struct SERVER_CONTEXT
 * @brief Libevent 기반 서버 역할의 Context
 *
 * - Listen 소켓 FD
 * - Accept 이벤트
 * - 연결된 클라이언트 리스트
 * - 클라이언트 수
 * - 상위 BASE_CONTEXT
 */
struct _SERVER_CONTEXT
{
    APP_ROLE            eRole;          /**< ROLE_TCP_SERVER 등 서버 역할 */
    int                 iListenFd;      /**< 서버 Listen FD */

    struct event*       pstAcceptEvent; /**< 서버용 accept 이벤트 */

    SOCK_CONTEXT*       pstClientList;  /**< 연결된 클라이언트 리스트 head */
    int                 iClientCount;   /**< 연결된 클라이언트 수 */

    BASE_CONTEXT*       pstBaseCtx;     /**< 공용 BASE_CONTEXT */
};


/* ========================================================================== */
/* PER-CONNECTION SOCKET CONTEXT                                              */
/* ========================================================================== */

/**
 * @struct _SOCK_CONTEXT
 * @brief 개별 연결(클라이언트)의 Session 상태를 저장
 *
 * bufferevent 기반으로 read/write/event를 수행한다.
 */
struct _SOCK_CONTEXT
{
    struct bufferevent* pstBufferEvent; /**< bufferevent 핸들 */

    BASE_CONTEXT*       pstBaseCtx;     /**< 상위 BASE_CONTEXT */
    SERVER_CONTEXT*     pstServerCtx;   /**< 소속 SERVER_CONTEXT (클라이언트 모드에서는 NULL 가능) */

    uint16_t            unCmd;          /**< 프로토콜 명령 코드(옵션) */
    int                 iDataLength;    /**< 수신 데이터 길이(옵션) */
    unsigned char       uchSrcId;       /**< 송신자 ID */
    unsigned char       uchDstId;       /**< 목적지 ID */
    unsigned char       uchIsResponse;  /**< 1: 응답 생성 / 0: 요청만 */

    struct _SOCK_CONTEXT* pstNextSockCtx; /**< 연결 리스트 next pointer */
    void*               pvUserCtx;
};


/* 응답 활성 여부 플래그 */
#define RESPONSE_ENABLED   1
#define RESPONSE_DISABLED  0


/* ========================================================================== */
/* PUBLIC API                                                                  */
/* ========================================================================== */

/**
 * @brief BASE_CONTEXT 구조체를 초기화한다.
 *
 * @param pstBaseCtx    초기화 대상 BASE_CONTEXT 포인터
 * @param uchMyId       애플리케이션 구분용 ID
 */
void baseContextInit(BASE_CONTEXT* pstBaseCtx,
                    unsigned char uchMyId);

/**
 * @brief SERVER_CONTEXT 구조체를 초기화한다.
 *
 * @param pstServerCtx  초기화 대상 SERVER_CONTEXT 포인터
 * @param pstBaseCtx    상위 BASE_CONTEXT 포인터
 * @param eAppRole      ROLE_TCP_SERVER 등 서버 역할
 */
void serverContextInit(SERVER_CONTEXT* pstServerCtx,
                    BASE_CONTEXT* pstBaseCtx,
                    APP_ROLE eAppRole);

/**
 * @brief SOCK_CONTEXT를 초기화하여 SERVER_CONTEXT와 연결한다.
 *
 * @param pstSockCtx    초기화할 SOCK_CONTEXT
 * @param pstServerCtx  소속 SERVER_CONTEXT
 * @param uchIsResponse RESPONSE_ENABLED / RESPONSE_DISABLED
 */
void initSocketContext(SOCK_CONTEXT* pstSockCtx,
                    SERVER_CONTEXT* pstServerCtx,
                    unsigned char uchIsResponse);

/**
 * @brief 서버 accept 이벤트 등록 및 활성화
 *
 * @param pstServerCtx SERVER_CONTEXT
 */
void setupServerAcceptEvent(SERVER_CONTEXT* pstServerCtx);

/**
 * @brief 클라이언트 모드 종료 처리 (stdin 이벤트 제거 + base loop exit)
 *
 * @param pstBaseCtx BASE_CONTEXT
 */
void shutdownApp(BASE_CONTEXT* pstBaseCtx);

/**
 * @brief bufferevent / 이벤트 / 소켓 자원을 해제하고 세션을 종료한다.
 *
 * @param pstSockCtx SOCK_CONTEXT
 */
void closeAndFree(SOCK_CONTEXT* pstSockCtx);

#ifdef __cplusplus
}
#endif

#endif /* EVENTSESSION_H */
