/**
 * @file icdCommand.c
 * @brief ICD 명령 처리 함수 구현부
 *
 * 본 파일은 keepAlive(), iBit() 명령 처리에 대한
 * 실제 실행 로직을 담당한다.
 */

#include "icdCommand.h"
#include <stdio.h>

/**
 * @brief Keep-Alive 명령 처리 (통신 활성 여부 확인)
 *
 * 장비 또는 소프트웨어 측에서 연결 유지 여부를 판단하기 위한 명령이며,
 * 응답은 항상 `chResult = 0x01` 로 설정된다.
 *
 * @param puchRecvData     수신 원본 요청 데이터 버퍼 (미사용)
 * @param puchCmdResult    응답 데이터가 기록될 버퍼
 *
 * @return 응답 데이터 크기 (sizeof(RES_KEEP_ALIVE))
 *
 * @see RES_KEEP_ALIVE
 */
int keepAlive(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	(void)puchRecvData; /* 사용하지 않음을 명시 */

	RES_KEEP_ALIVE *pstResKeepAlive =
		(RES_KEEP_ALIVE *)(puchCmdResult);

	pstResKeepAlive->chResult = 0x01;

	fprintf(stderr, "ICD_KEEP_ALIVE executed\n");
	return sizeof(RES_KEEP_ALIVE);
}


/**
 * @brief 초기 Built-In-Test (IBIT) 명령 처리 함수
 *
 * 장비의 상태를 확인하기 위한 테스트 기능이며
 * 총 결과와 Position 결과를 모두 `0x01` 로 반환한다.
 *
 * @param puchRecvData     수신 원본 요청 데이터 버퍼(미사용)
 * @param puchCmdResult    응답 데이터 저장 버퍼
 *
 * @return 응답 데이터 크기(sizeof(RES_IBIT))
 *
 * @see RES_IBIT
 */
int iBit(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	(void)puchRecvData;

	RES_IBIT *pstResIBit =
		(RES_IBIT *)(puchCmdResult);

	pstResIBit->chBitTotResult    = 0x01;
	pstResIBit->chPositionResult  = 0x01;

	fprintf(stderr, "ICD_IBIT executed\n");
	return sizeof(RES_IBIT);
}
