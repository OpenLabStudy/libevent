/**
 * @file netTcp.h
 * @brief TCP 서버/클라이언트 소켓 생성 유틸리티
 *
 * 본 헤더는 TCP 기반 소켓 생성 기능을 제공하는 API를 정의한다.
 * - TCP 서버 생성(bind + listen)
 * - TCP 클라이언트 생성(non-blocking connect)
 *
 * @see netTcp.c
 */

#ifndef NETTCP_H
#define NETTCP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRACKING_CTRL_SVR   1141
#define AZ_EL_SENDER        1142
#define KEYBOARD_RECEIVER   1143

#define TCP_SVR_ID      0xB1
#define TCP_CLN_ID      0x10


/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * @brief Non-blocking TCP 서버 소켓을 생성한다.
 *
 * 내부적으로 다음 시스템 호출을 수행한다:
 * - socket(AF_INET, SOCK_STREAM)
 * - setsockopt(SO_REUSEADDR)
 * - bind()
 * - listen()
 *
 * @param unPort 서버가 바인딩할 포트 번호
 *
 * @return 생성된 서버 리슨 소켓 FD  
 * @return -1 오류 발생 (socket/bind/listen 실패)
 *
 * @note 반환된 소켓은 Non-blocking 상태이며 FD_CLOEXEC 설정이 적용됨
 */
int netTcpCreateServer(uint16_t unPort);

/**
 * @brief Non-blocking TCP 클라이언트 소켓을 생성하고 connect()를 시도한다.
 *
 * connect() 호출 시 non-blocking 소켓이므로 EINPROGRESS가 정상 상황일 수 있다.
 *
 * @param pszIp    문자열 형식 IPv4 주소 ("192.168.0.10" 등)
 * @param unPort   원격 서버 포트 번호
 *
 * @return 연결 시도 중인 소켓 FD  
 * @return -1 오류 발생 (socket 실패)
 *
 * @note 반환된 소켓은 libevent bufferevent_socket_new()로 넘겨 사용 가능
 */
int netTcpCreateClient(const char* pszIp, uint16_t unPort);

#ifdef __cplusplus
}
#endif

#endif /* NETTCP_H */
 