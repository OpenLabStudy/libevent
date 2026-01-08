/**
 * @file netUds.h
 * @brief UNIX Domain Socket 기반 TCP 스타일 통신 소켓 생성 유틸리티
 *
 * 본 헤더는 UNIX 도메인 소켓(AF_UNIX)을 사용한 고속 IPC (Inter-Process Communication)
 * 연결 생성 API를 정의하며, 로컬 프로세스 간 통신 환경에서 TCP보다 낮은 오버헤드를 제공한다.
 */

#ifndef NETUDS_H
#define NETUDS_H

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UDS_1_PATH              "/tmp/uds1.sock"
#define UDS_1_SVR_ID            0x00
#define UDS_1_SENSOR_FUSION     0x01
#define UDS_1_ACU_CONTROLLER    0x02

#define UDS_2_PATH              "/tmp/uds2.sock"
#define UDS_2_SVR_ID            0x30
#define UDS_2_GPS_RECEIVER      0x11
#define UDS_2_IMU_RECEIVER      0x12
#define UDS_2_SP_RECEIVER       0x14
#define UDS_2_EXTERN_RECEIVER   0x18
#define UDS_2_KEYBOARD_RECEIVER 0x21

#define UDS_3_PATH              "/tmp/uds3.sock"
#define UDS_3_SVR_ID            0x40
#define UDS_3_SENSOR_FUSION     0x41


#define UDS_4_PATH              "/tmp/uds4.sock"
#define UDS_4_SVR_ID            0x80
#define UDS_4_KEYBOARD_RECEIVER 0x81

#define UDS_MAX_BUFFER_SIZE     2048




/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * @brief UDS (Unix Domain Socket) 서버 소켓 생성
 *
 * 기존 동일한 소켓 path가 존재할 경우 `unlink()` 후 새로 생성한다.
 * Non-blocking, close-on-exec 플래그가 적용되며 listen 상태로 진입한다.
 *
 * @param pszPath 소켓 파일 경로 (예: "/tmp/imu_socket")
 *
 * @return 리슨 소켓 FD  
 * @return -1 오류 (socket / bind / listen 실패)
 *
 * @note 소켓 파일은 로컬 파일 시스템(`sun_path`)을 사용하며 최대 길이 제한이 존재함
 */
int netUdsCreateServer(const char* pszPath);

/**
 * @brief UDS 클라이언트 연결 생성 (non-blocking connect)
 *
 * TCP와 달리 원격 주소가 아닌 파일 시스템 경로를 통해 서버에 연결한다.
 *
 * @param pszPath 서버 소켓 파일 경로
 *
 * @return 연결 중인 클라이언트 소켓 FD  
 * @return -1 오류 발생 (socket 실패)
 *
 * @note UDS는 loopback 네트워크를 사용하지 않으며 매우 낮은 지연시간을 제공함
 */
int netUdsCreateClient(const char* pszPath);

#ifdef __cplusplus
}
#endif

#endif /* NETUDS_H */
 