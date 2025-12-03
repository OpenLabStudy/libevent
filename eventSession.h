/**
 * @file eventSession.h
 * @brief Libevent 기반 TCP Session 관리 모듈 (Server / Client 공용)
 *
 * 본 헤더는 Libevent 기반 세션 구성 요소(EVENT_CONTEXT, SOCK_CONTEXT),
 * 콜백 핸들러, 초기화 함수, 이벤트 등록 함수 등의 인터페이스를 정의한다.
 *
 * - 서버/클라이언트 공통 구조체 제공
 * - bufferevent 기반 비동기 Read/Write 처리
 * - accept 이벤트 기반 다중 접속 처리
 * - 콜백 주입 방식(read/write/event)
 *
 * @see eventSession.c
 */

#ifndef EVENTSESSION_H
#define EVENTSESSION_H

#include <stdint.h>
#include <event2/event.h>
#include <event2/bufferevent.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* ENUM & DEFINES                                                             */
/* ========================================================================== */

/**
 * @enum APP_ROLE
 * @brief 프로그램 동작 역할(Server 또는 Client)
 *
 * @var ROLE_SERVER 서버 역할
 * @var ROLE_CLIENT 클라이언트 역할
 */
typedef enum
{
    ROLE_SERVER         = 0x10,
    ROLE_TCP_SERVER     = 0x11, /**< 서버 역할 */
    ROLE_UDP_SERVER     = 0x12, /**< 서버 역할 */
    ROLE_UDS_SERVER     = 0x13, /**< 서버 역할 */
    ROLE_CLIENT         = 0x20,
    ROLE_TCP_CLIENT     = 0x21,  /**< 클라이언트 역할 */
    ROLE_UDP_CLIENT     = 0x22,  /**< 클라이언트 역할 */
    ROLE_UDS_CLIENT     = 0x23  /**< 클라이언트 역할 */   
} APP_ROLE;


/* Forward Declaration */
struct _SOCK_CONTEXT;
typedef struct _SOCK_CONTEXT SOCK_CONTEXT;


/* ========================================================================== */
/* CALLBACK HANDLER SET                                                       */
/* ========================================================================== */

/**
 * @struct EVENT_HANDLER
 * @brief 사용자 애플리케이션에서 주입하는 Libevent 콜백 함수 세트
 *
 * @var EVENT_HANDLER::pfReadCb   bufferevent Read callback
 * @var EVENT_HANDLER::pfWriteCb  bufferevent Write callback (옵션 NULL 가능)
 * @var EVENT_HANDLER::pfEventCb  bufferevent Event callback (EOF/ERROR 등)
 */
typedef struct _EVENT_HANDLER
{
    bufferevent_data_cb   pfReadCb;   /**< Read Callback */
    bufferevent_data_cb   pfWriteCb;  /**< Write Callback (NULL=미사용) */
    bufferevent_event_cb  pfEventCb;  /**< Event Callback */
} EVENT_HANDLER;


/* ========================================================================== */
/* EVENT CONTEXT (SERVER / CLIENT 공용)                                       */
/* ========================================================================== */

/**
 * @struct EVENT_CONTEXT
 * @brief Libevent 기반 Session의 최상위 Context 구조체
 *
 * 서버/클라이언트 모드를 포함하고 이벤트 루프, 소켓 및 bufferevent
 * 상태를 포함한다.
 */
typedef struct _EVENT_CONTEXT
{
    APP_ROLE            eRole;          /**< ROLE_SERVER / ROLE_CLIENT */
    int                 iSockFd;        /**< Listen FD(서버) 또는 연결 FD(클라) */

    struct event_base*  pstEventBase;   /**< Libevent Base */
    struct event*       pstEvent;       /**< stdin 등 기타 이벤트 */
    struct event*       pstSignalEvent; /**< Signal 이벤트 */
    struct event*       pstAcceptEvent; /**< 서버용 accept 이벤트 */

    SOCK_CONTEXT*       pstSockCtx;     /**< 연결된 클라이언트 리스트 (서버 Only) */
    int                 iClientCount;   /**< 연결된 클라이언트 수 */

    unsigned char       uchMyId;        /**< 송신자(자기 ID), 프레임 통신 시 활용 */

    EVENT_HANDLER       stHandler;      /**< 애플리케이션 제공 콜백 */
    void* pvUserCtx;
} EVENT_CONTEXT;


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
    EVENT_CONTEXT*      pstEventCtx;    /**< 상위 EVENT_CONTEXT */

    uint16_t            unCmd;          /**< 프로토콜 명령 코드(옵션) */
    int                 iDataLength;    /**< 수신 데이터 길이(옵션) */
    unsigned char       uchSrcId;       /**< 송신자 ID */
    unsigned char       uchDstId;       /**< 목적지 ID */
    unsigned char       uchIsResponse;  /**< 1: 응답 생성 / 0: 요청만 */

    struct _SOCK_CONTEXT* pstNextSockCtx; /**< 연결 리스트 next pointer */
    void* pvUserCtx;
};


/* 응답 활성 여부 플래그 */
#define RESPONSE_ENABLED   1
#define RESPONSE_DISABLED  0


/* ========================================================================== */
/* PUBLIC API                                                                  */
/* ========================================================================== */

/**
 * @brief EVENT_CONTEXT 구조체를 초기화한다.
 *
 * @param pstEventCtx   초기화 대상 EVENT_CONTEXT 포인터
 * @param eAppRole      ROLE_SERVER 또는 ROLE_CLIENT
 * @param uchMyId       애플리케이션 구분용 ID
 */
void initEventContext(EVENT_CONTEXT* pstEventCtx,
                    APP_ROLE eAppRole,
                    unsigned char uchMyId);

/**
 * @brief SOCK_CONTEXT를 초기화하여 EVENT_CONTEXT와 연결한다.
 *
 * @param pstSockCtx    초기화할 SOCK_CONTEXT
 * @param pstEventCtx   상위 EVENT_CONTEXT
 * @param uchIsResponse 응답 여부(RESPONSE_ENABLED/RESPONSE_DISABLED)
 */
void initSocketContext(SOCK_CONTEXT* pstSockCtx,
                    EVENT_CONTEXT* pstEventCtx,
                    unsigned char uchIsResponse);

/**
 * @brief 서버 모드에서 accept 가능하도록 이벤트를 등록한다.
 *
 * @param pstEventCtx EVENT_CONTEXT (ROLE_SERVER 환경에서만 사용)
 */
void setupServerAcceptEvent(EVENT_CONTEXT* pstEventCtx);

/**
 * @brief 클라이언트 모드 종료 처리 (event loop 중단 포함)
 *
 * @param pstEventCtx EVENT_CONTEXT
 */
void shutdownApp(EVENT_CONTEXT* pstEventCtx);

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
