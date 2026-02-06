/*
 * acuUtils.c
 *
 *  Created on: 2020. 6. 17.
 *      Author: pcw1029
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "acuUtil.h"

/**
 * @brief 인자로 전달받은 방위각/고각 정보 포지션 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
int moveAzElPosition(double i_dAz, double i_dEl, char* pchOutData)
{
	memset(pchOutData, 0x0, ACU_COMMAND_LEN);
	sprintf(pchOutData, "%s%.3f;%.3f\r\n", AZ_REMOTE_POSITION_SET, i_dAz, i_dEl);
	// fprintf(stderr,"### %s():%d %s ###\n",__func__,__LINE__, pchOutData);
	return strlen(pchOutData);
}

/**
 * @brief 인자로 전달받은 방위각/고각 정보 RATE 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
int moveAzElRate(double i_dAz, double i_dEl, char* pchOutData)
{
	memset(pchOutData, 0x0, ACU_COMMAND_LEN);
	sprintf(pchOutData, "%s%.3f;%.3f\r\n", AZ_REMOTE_RATE_SET, i_dAz, i_dEl);
	return strlen(pchOutData);
}

/**
 * @brief 인자로 전달받은 방위각/고각 정보 RATE 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
int moveAzRate(int i_iAcuFd, double i_dAz, char* pchOutData)
{
	(void)i_iAcuFd;
	memset(pchOutData, 0x0, ACU_COMMAND_LEN);
	sprintf(pchOutData, "%s%.3f\r\n", AZ_REMOTE_RATE_SET, i_dAz);
	return strlen(pchOutData);
}

/**
 * @brief 인자로 전달받은 방위각/고각 정보 RATE 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
int moveElRate(int i_iAcuFd, double i_dEl, char* pchOutData)
{
	(void)i_iAcuFd;
	memset(pchOutData, 0x0, ACU_COMMAND_LEN);
	sprintf(pchOutData, "%s%.3f\r\n", EL_REMOTE_RATE_SET, i_dEl);
	return strlen(pchOutData);
}


/**
 * @brief 문자열을 chSeparate을 기준으로 분리하는 함수
 * @param chpStringData 분리하고자 하는 문자열
 * @param chSeparate 분리 기준이 되는 문자
 * @param chppStorage 분리된 문자열들을 저장할 변수
 * @param iMaxSplitCount 분리된 문자열 개수
 * @return 분리된 문자열 개수
 */
int splitAcuDataString(char* chpStringData, char chSeparate, char** chppStorage, int iMaxSplitCount){
	int iStringCount=0;
	char* chpSplitString = chpStringData;
	char* chpEndOfSplitString;

	chppStorage[iStringCount++] = chpSplitString;

	while(1){
		chpEndOfSplitString=strchr(chpSplitString,chSeparate);
		if(chpEndOfSplitString == NULL)
			break;
		if(iStringCount < iMaxSplitCount){
			*chpEndOfSplitString=0;
			chpSplitString=chpEndOfSplitString+1;
			chppStorage[iStringCount++]=chpSplitString;
		}else
			break;
	}
	return iStringCount;
}

int modeChange(char i_chMode, char* pchOutData)
{
	memset(pchOutData, 0x0, ACU_COMMAND_LEN);
	if(i_chMode == 0x01){//POSITION MODE		
		strcpy(pchOutData, REMOTE_POSITION_MODE);
	}else if(i_chMode == 0x00){//RATE MODE
		strcpy(pchOutData, REMOTE_RATE_MODE);		
	}
	return strlen(pchOutData);
}

int readAzElFromAcu(char* pchOutData){
	memset(pchOutData, 0x0, ACU_COMMAND_LEN);
	strcpy(pchOutData, READ_AZ_EL_INFO);
	return strlen(pchOutData);
}

