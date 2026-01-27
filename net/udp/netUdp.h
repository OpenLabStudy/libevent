/**
 * @file netUdp.h
 * @brief UDP 기반 서버 및 클라이언트 소켓 생성 유틸리티
 *
 * 본 헤더는 UDP 서버 및 UDP 클라이언트용 소켓 생성 기능을 제공한다.
 * - Non-blocking socket 생성
 * - FD_CLOEXEC 옵션 적용
 * - 선택적으로 connect() 호출을 수행하여 send()/recv() 사용을 단순화함
 */

#ifndef NETUDP_H
#define NETUDP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UDP_SVR_ID      0x77
#define UDP_CLN_ID      0x55

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * @brief UDP 서버용 Non-blocking 소켓 생성 (bind + optional connect)
 *
 * 서버는 `bind()`를 통해 지정된 포트를 listen 상태로 오픈하며,
 * 지정된 클라이언트 주소가 전달될 경우 `connect()`를 호출해 단일 피어와
 * 통신하도록 최적화할 수 있다.
 *
 * @param unPort         서버가 bind할 UDP 포트 번호
 * @param pchClientIp    연결 허용 또는 타깃 UDP 피어 IP (NULL 허용 안함)
 * @param unClientPort   연결할 클라이언트의 포트 번호
 *
 * @return 생성된 UDP 서버 소켓 FD  
 * @return -1 오류 발생 (socket/bind/connect 실패)
 *
 * @note 반환된 FD는 Non-blocking 상태이며 libevent bufferevent에 전달 가능
 */
int netUdpCreateServer(uint16_t unPort,
                    const char* pchClientIp,
                    uint16_t unClientPort);

/**
 * @brief Non-blocking UDP 클라이언트 소켓 생성 및 connect() 수행
 *
 * UDP는 Connectionless 방식이므로 connect()는 실제 TCP와 다르게 SYN 패킷을 생성하지 않지만,
 * 이후 `send()`/`recv()`를 사용할 수 있고 불필요한 주소 인자를 반복 입력할 필요가 없어진다.
 *
 * @param pszIp       서버 IP (ex: "127.0.0.1")
 * @param unSrvPort   UDP 서버 포트 번호
 * @param unMyPort    로컬 바인딩 포트 (0 → 자동)
 *
 * @return 생성된 Non-blocking UDP 소켓 FD  
 * @return -1 socket 생성 실패
 */
int netUdpCreateClient(const char* pszIp,
                    uint16_t unSrvPort,
                    uint16_t unMyPort);

#ifdef __cplusplus
}
#endif

#endif /* NETUDP_H */
