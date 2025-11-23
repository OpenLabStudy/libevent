/**
 * @file udpSvr.h
 * @brief Libevent 기반 UDP 서버 Application API 헤더
 *
 * 본 헤더는 udpSvr.c의 외부 실행 진입점(run 함수)을 공개하며,
 * GoogleTest 또는 실제 애플리케이션 환경에서 UDP 서버를 실행할 수 있도록 한다.
 *
 * ### 제공 기능
 * - UDP 서버 실행(run)
 * - GoogleTest 환경에서 main() 함수 제거
 *
 * UDP는 TCP와 달리 연결 기반이 아니며 데이터그램 단위로 통신하므로
 * bufferevent 대신 event_new() 기반 이벤트 처리 모델을 사용한다.
 *
 * @see udpSvr.c
 */

 #ifndef UDP_SVR_H
 #define UDP_SVR_H
 
 #include "netUdp.h"
 #include "netCore.h"
 #include "eventSession.h"
 #include "frame.h"

#define SERVER_IP         "127.0.0.1"
#define CLIENT_IP         "127.0.0.1"
#define UDP_SERVER_PORT   5001
#define UDP_CLIENT_PORT   5002
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /**
  * @brief UDP Server 실행 함수
  *
  * Libevent 기반 이벤트 루프를 생성하고 UDP 소켓을 바인딩한 후,
  * EV_READ 이벤트 기반으로 수신 데이터를 처리한다.
  *
  * GoogleTest 실행 환경에서는 main() 함수 대신 해당 함수를 호출하여
  * 테스트 측에서 서버 실행 여부를 제어한다.
  *
  * @return 0 정상 종료  
  * @return -1 오류 발생
  */
 int run(void);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* UDP_SVR_H */
 