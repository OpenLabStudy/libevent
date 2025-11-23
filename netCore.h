/**
 * @file netCore.h
 * @brief POSIX 기반 소켓 TCP 통신 환경 설정 유틸리티
 *
 * 본 헤더에는 소켓의 블록/논블록 상태 설정, 재사용 옵션 설정,
 * FD close-on-exec 설정 및 안전한 close 래퍼 함수 정의를 포함한다.
 *
 * 이 모듈은 OS 수준 네트워크 설정과 직접 연결되어 있으며,
 * Libevent, TCP 서버/클라이언트 모듈에서 공통으로 사용된다.
 */

#ifndef NETCORE_H
#define NETCORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * @brief 지정된 소켓을 Non-blocking 모드로 변경한다.
 *
 * @param iFd  대상 File Descriptor (socket FD)
 *
 * @return 0  성공  
 * @return -1 실패 (fcntl 내부 오류)
 *
 * @see fcntl(), O_NONBLOCK
 */
int netSetNonblock(int iFd);

/**
 * @brief SO_REUSEADDR 옵션을 활성화하여 TIME_WAIT 소켓의 bind() 실패 문제를 완화한다.
 *
 * @param iFd  대상 socket FD
 *
 * @return 0 성공  
 * @return -1 실패
 *
 * @see setsockopt(), SO_REUSEADDR
 */
int netSetReuseAddr(int iFd);

/**
 * @brief FD에 FD_CLOEXEC 플래그를 설정하여 exec() 이후 소켓이 자동으로 닫히도록 한다.
 *
 * @param iFd 대상 socket FD
 *
 * @return 0 성공  
 * @return -1 실패
 *
 * @see fcntl(), FD_CLOEXEC
 */
int netSetCloexec(int iFd);

/**
 * @brief 안전한 socket close() 래퍼 함수
 *
 * @param iFd 닫을 socket FD
 *
 * @return 0 성공 또는 이미 무효 FD  
 * @return -1 close 실패
 *
 * @note iFd가 -1 이하일 경우 close() 호출하지 않음
 */
int netClose(int iFd);

#ifdef __cplusplus
}
#endif

#endif /* NETCORE_H */
 