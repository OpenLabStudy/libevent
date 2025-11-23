/**
 * @file tcpSvr.h
 * @brief Libevent 기반 TCP 서버 Application API 헤더
 *
 * 이 헤더는 tcpSvr.c의 외부 인터페이스(run 함수)를 제공하며,
 * 네트워크/프레임/세션 계층을 연결하는 최상위 애플리케이션 진입점을 정의한다.
 *
 * ### 주요 포함 기능
 * - TCP 서버 실행(run)
 * - GoogleTest 환경에서 main 함수 제외
 *
 * @see tcpSvr.c
 */

#ifndef TCP_SVR_H
#define TCP_SVR_H

#include "netTcp.h"
#include "netCore.h"
#include "eventSession.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP 서버 실행 진입 함수
 *
 * Libevent 기반 TCP 서버를 초기화하고 event loop에 진입한다.
 * 일반 실행 환경에서는 main() 함수에서 호출되며,
 * GoogleTest 환경에서는 서버 실행 테스트를 위해 직접 호출된다.
 *
 * @return 0 정상 종료  
 * @return -1 오류 발생
 */
int run(void);

#ifdef __cplusplus
}
#endif

#endif /* TCP_SVR_H */
 