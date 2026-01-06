/*
 * acuUtil.h
 *
 *  Created on: 2020. 6. 17.
 *      Author: pcw1029
 */

#ifndef SRC_ACUUTILS_H_
#define SRC_ACUUTILS_H_

#ifdef __cplusplus
extern "C"{
#endif


#include <stdbool.h>

#define REMOTE_RATE_MODE 			"P02W18\r\n"
#define REMOTE_POSITION_MODE 	    "P02W3\r\n"
#define AZ_REMOTE_RATE_MODE 		"PF0W18\r\n"
#define AZ_REMOTE_POSITION_MODE     "PF0W3\r\n"
#define EL_REMOTE_RATE_MODE 		"PF1W18\r\n"
#define EL_REMOTE_POSITION_MODE	    "PF1W3\r\n"

#define AZ_MODE_STATUS				"P0F2\r\n"
#define EL_MODE_STATUS				"P0F3\r\n"

#define AZ_REMOTE_POSITION_SET		"P00W"
#define EL_REMOTE_POSITION_SET		"P01W"
#define AZ_REMOTE_RATE_SET			"P290W"
#define EL_REMOTE_RATE_SET			"P291W"

#define AZ_BIT 						"P387R1\r\n"
#define EL_BIT 						"P388R1\r\n"

#define READ_AZ_EL_INFO				"P07R2\r\n"

#define BIT_SUCCESS					"00000000\r\n"
#define CMD_SUCCESS					0x06

#define ACU_COMMAND_LEN 			64
#define AZ_BIT_FLAG					0x01
#define EL_BIT_FLAG					0x02
#define POSITION_SLAVE				0x03
#define RATE_SLAVE					0x04

typedef struct {
	double dAz;
	double dEl;
}AZ_EL_INFO;

bool moveAzElRate(int i_iAcuFd, double i_dAz, double i_dEl);
bool moveAzRate(int i_iAcuFd, double i_dAz);
bool moveElRate(int i_iAcuFd, double i_dEl);

bool moveAzElPosition(int i_iAcuFd, double i_dAz, double i_dEl);

/**
 * @brief ACU구조체 초기화(ACU와 시리얼 연결 및 변수 초기화)
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
//bool acuStructInit(ACU* i_pstAcu);

/**
 * @brief ACU DSA Online BIT 수행
 * @param i_iAcuFd ACU와 연결된 시리얼 파일 디스크립터
 * @param i_chBitFlag BIT선택 플래그(AZ DSA Online BIT or EL DSA Online BIT)
 * @return BIT명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool bitCheck(int i_iAcuFd, char i_chBitFlag);

/**
 * @brief ACU 동작 모드 변경(POSITION or RATE)
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_chMode 동작 모드 변경 플래그(POSITION or RATE)
 * @return 동작 모드 변경 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool modeChange(int i_iAcuFd, char i_chMode);

/**
 * @brief ACU 현재 방위각/고각 읽기
 * @param i_iAcuFd ACU와 연결된 시리얼 파일 디스크립터
 * @param i_pstReadAzEl ACU로 부터 읽어들인 방위각/고각 저장 구조체
 * @return ACU 현재 방위각/고각 읽기 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool readAzElFromAcu(int i_iAcuFd, AZ_EL_INFO* i_pstReadAzEl);


#ifdef __cplusplus
}
#endif

#endif /* SRC_ACUUTILS_H_ */