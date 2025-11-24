/**
 * @file udsClientTable.h
 * @brief UDS(Unix Domain Socket) 기반 클라이언트 세션 관리 모듈
 *
 * 본 모듈은 UDS 클라이언트의 연결 상태를 관리하고,
 * ID 기반 조회, 삭제, 브로드캐스트 전송 지원 기능을 제공한다.
 *
 * ### 주요 기능
 * - 신규 연결 클라이언트 등록
 * - bufferevent 기반 삭제 처리
 * - ID 기반 세션 조회
 * - 마스크 기반 브로드캐스트 전송 지원
 *
 * @note Thread-safe 설계가 아님 (단일 이벤트 루프 모델 운영)
 */

#ifndef UDS_CLIENT_TABLE_H
#define UDS_CLIENT_TABLE_H

#include <event2/bufferevent.h>
#include <stdbool.h>

#define UDS_MAX_CLIENT 32

/**
 * @struct UDS_CLIENT_ENTRY
 * @brief 단일 UDS 클라이언트 연결 상태를 표현하는 구조체
 */
typedef struct
{
    bool                    bActive;            /**< 연결 여부 */
    unsigned char           uchClientId;        /**< Application Logical Client ID */
    struct bufferevent*     pstBev;             /**< libevent bufferevent 객체 */
} UDS_CLIENT_ENTRY;


/**
 * @struct UDS_CLIENT_TABLE
 * @brief 등록된 UDS 클라이언트 목록 구조체
 */
typedef struct
{
    UDS_CLIENT_ENTRY        astEntry[UDS_MAX_CLIENT];
    int                     iCount;
} UDS_CLIENT_TABLE;


/* ========================================================================== */
/* API Prototypes                                                             */
/* ========================================================================== */

/**
 * @brief UDS 클라이언트 테이블 초기화
 *
 * @param[out] pstTable 클라이언트 테이블
 */
void udsClientTableInit(UDS_CLIENT_TABLE* pstTable);


/**
 * @brief 신규 UDS 클라이언트 등록
 *
 * @param[in,out] pstTable      테이블 포인터
 * @param[in]     pstBev        bufferevent 객체
 * @param[in]     uchClientId   라우팅 대상 ID
 * @return true  등록 성공 / false 실패
 */
bool udsClientRegister(UDS_CLIENT_TABLE* pstTable,
                    struct bufferevent* pstBev,
                    unsigned char uchClientId);


/**
 * @brief Client ID 기반 bufferevent 조회
 *
 * @param pstTable   테이블 포인터
 * @param uchClientId 조회할 ID
 * @return struct bufferevent* NULL이면 존재하지 않음
 */
struct bufferevent* udsClientGetBev(const UDS_CLIENT_TABLE* pstTable,
                                    unsigned char uchClientId);


/**
 * @brief bufferevent 기반 삭제
 *
 * @param pstTable 테이블 포인터
 * @param pstBev   삭제할 bufferevent
 */
void udsClientUnregisterByBev(UDS_CLIENT_TABLE* pstTable,
                            struct bufferevent* pstBev);


/**
 * @brief Bitmask 기반 브로드캐스트 전송
 *
 * @param pstTable 테이블 포인터
 * @param unMask   대상 비트마스크
 * @param pchData  전송할 데이터 포인터
 * @param iLen     데이터 길이
 * @return int     실제 전송된 UDS Client 수
 */
int udsClientBroadcastMask(const UDS_CLIENT_TABLE* pstTable,
                        unsigned int unMask,
                        const unsigned char* pchData,
                        int iLen);

#endif // UDS_CLIENT_TABLE_H
 