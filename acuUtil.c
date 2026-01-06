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
#include "uartConfig.h"

/**
 * @brief 인자로 전달받은 데이터를 ACU로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool sendCommandToAcu(int i_iAcuFd, char* i_chWriteBuff)
{
	int iReadSize;
    int iWriteSize;
	char chReadBuff[ACU_COMMAND_LEN];

	memset(chReadBuff, 0x0, ACU_COMMAND_LEN);
	iWriteSize = write(i_iAcuFd, i_chWriteBuff, strlen(i_chWriteBuff));    
	iReadSize = read(i_iAcuFd, chReadBuff, ACU_COMMAND_LEN);

	if(iReadSize <= 0)
		return false;

	if(chReadBuff[0] == 0x06){
		return true;
	}else if(chReadBuff[0] == 0x15){
		fprintf(stderr,"write to ACU Error[%02x]\n",chReadBuff[0]);
	}else{
		fprintf(stderr,"response value is unknown.[%02X]\n", chReadBuff[0]);
	}
	return false;
}

/**
 * @brief 인자로 전달받은 방위각/고각 정보 포지션 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool moveAzElPosition(int i_iAcuFd, double i_dAz, double i_dEl)
{
	char chWriteBuff[ACU_COMMAND_LEN];
	memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
	sprintf(chWriteBuff, "%s%.3f;%.3f\r\n", AZ_REMOTE_POSITION_SET, i_dAz, i_dEl);
	if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
		fprintf(stderr,"ACU POSITION MODE Az/El Setting fail..[%s]", chWriteBuff);
		return false;
	}
	return true;
}

/**
 * @brief 인자로 전달받은 방위각/고각 정보 RATE 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool moveAzElRate(int i_iAcuFd, double i_dAz, double i_dEl)
{
	char chWriteBuff[ACU_COMMAND_LEN];
	memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
	sprintf(chWriteBuff, "%s%.3f;%.3f\r\n", AZ_REMOTE_RATE_SET, i_dAz, i_dEl);
	fprintf(stderr,"ACU SEND DATA %s", chWriteBuff);
	if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
		fprintf(stderr,"ACU RATEMODE MODE Az/El Setting fail..[%s]", chWriteBuff);
		return false;
	}
	return true;
}

/**
 * @brief 인자로 전달받은 방위각/고각 정보 RATE 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool moveAzRate(int i_iAcuFd, double i_dAz)
{
	char chWriteBuff[ACU_COMMAND_LEN];
	memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
	sprintf(chWriteBuff, "%s%.3f\r\n", AZ_REMOTE_RATE_SET, i_dAz);
	fprintf(stderr,"ACU SEND DATA %s", chWriteBuff);
	if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
		fprintf(stderr,"ACU RATEMODE MODE Az Setting fail..[%s]", chWriteBuff);
		return false;
	}
	return true;
}

/**
 * @brief 인자로 전달받은 방위각/고각 정보 RATE 모드로 전송
 * @param i_pstAcu ACU구조체(acu와 연결된 시리얼 파일디스크립트, acu로 전송할 데이터 참조)
 * @param i_pstAzEl 방위각/고각 정보
 * @return 명령 수행 결과 반환 [성공 : true , 실패 : false]
 */
bool moveElRate(int i_iAcuFd, double i_dEl)
{
	char chWriteBuff[ACU_COMMAND_LEN];
	memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
	sprintf(chWriteBuff, "%s%.3f\r\n", EL_REMOTE_RATE_SET, i_dEl);
	fprintf(stderr,"ACU SEND DATA %s", chWriteBuff);
	if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
		fprintf(stderr,"ACU RATEMODE MODE El Setting fail..[%s]", chWriteBuff);
		return false;
	}
	return true;
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


// bool bitCheck(int i_iAcuFd, char i_chBitFlag)
// {
// 	int iReadSize;
// 	char chWriteBuff[ACU_COMMAND_LEN];
// 	char chReadBuff[ACU_COMMAND_LEN];

// 	memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
// 	if(i_chBitFlag == AZ_BIT_FLAG){
// 		strcpy(chWriteBuff, AZ_BIT);
// 	}else if(i_chBitFlag == EL_BIT_FLAG){
// 		strcpy(chWriteBuff, EL_BIT);
// 	}
// 	uartWrite(i_iAcuFd, chWriteBuff, strlen(chWriteBuff));

// 	memset(chReadBuff, 0x0, ACU_COMMAND_LEN);
// 	iReadSize = uartReadLF(i_iAcuFd, chReadBuff, ACU_COMMAND_LEN);
// 	if(iReadSize <= 0){
// 		fprintf(stderr,"ACU BIT Fail..\n");
// 		return false;
// 	}

// 	if(strcmp(chReadBuff, "00000000\r\n") == 0){
// 		return true;
// 	}else{
// 		for(int i=0; i<iReadSize; i++)
// 			fprintf(stderr,"%d.%02X  ",i, chReadBuff[i]);
// 		fprintf(stderr,"\n");
// 		return false;
// 	}
// }

bool modeChange(int i_iAcuFd, char i_chMode)
{
	char chWriteBuff[ACU_COMMAND_LEN];
	if(i_chMode == POSITION_SLAVE){
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, REMOTE_POSITION_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"REMOTE_POSITION_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, AZ_REMOTE_POSITION_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"AZ_REMOTE_POSITION_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, EL_REMOTE_POSITION_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"EL_REMOTE_POSITION_MODE Setting Fail..\n");
			return false;
		}
	}else if(i_chMode == RATE_SLAVE){
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, REMOTE_RATE_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"REMOTE_RATE_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, AZ_REMOTE_RATE_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"AZ_REMOTE_RATE_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, EL_REMOTE_RATE_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"EL_REMOTE_RATE_MODE Setting Fail..\n");
			return false;
		}
	}
	return true;
}

bool readAzElFromAcu(int i_iAcuFd, AZ_EL_INFO* i_pstReadAzEl){
	int iReadSize;
	int iSplitCnt;
	char *chSplitData[8];
	char chWriteData[ACU_COMMAND_LEN];
	char chReadData[ACU_COMMAND_LEN];

	memset(chWriteData, 0x0, ACU_COMMAND_LEN);
	memset(chReadData, 0x0, ACU_COMMAND_LEN);

	sprintf(chWriteData, READ_AZ_EL_INFO);
	uartWrite(i_iAcuFd, chWriteData, strlen(chWriteData));
	iReadSize = uartReadLF(i_iAcuFd, chReadData, ACU_COMMAND_LEN);
	if(iReadSize <= 0){
		fprintf(stderr,"No data was received from the serial device.\n");
		return false;
	}
	iSplitCnt = splitAcuDataString(chReadData, ';', chSplitData, 2);
	if(iSplitCnt != 2){
		fprintf(stderr,"The received serial data is abnormal.\n");
		fprintf(stderr,"Serial recv data : %s\n", chReadData);
		return false;
	}

	i_pstReadAzEl->dAz = atof(chSplitData[0]);
	i_pstReadAzEl->dEl = atof(chSplitData[1]);

	return true;;
}


bool modeChangeForCheckAcu(int i_iAcuFd, char i_chMode)
{
	char chWriteBuff[ACU_COMMAND_LEN];
	if(i_chMode == POSITION_SLAVE){
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, REMOTE_POSITION_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"REMOTE_POSITION_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, AZ_REMOTE_POSITION_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"AZ_REMOTE_POSITION_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, EL_REMOTE_POSITION_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"EL_REMOTE_POSITION_MODE Setting Fail..\n");
			return false;
		}
	}else if(i_chMode == RATE_SLAVE){
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, REMOTE_RATE_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"REMOTE_RATE_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, AZ_REMOTE_RATE_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"AZ_REMOTE_RATE_MODE Setting Fail..\n");
			return false;
		}
		memset(chWriteBuff, 0x0, ACU_COMMAND_LEN);
		strcpy(chWriteBuff, EL_REMOTE_RATE_MODE);
		if(sendCommandToAcu(i_iAcuFd, chWriteBuff) == false){
			fprintf(stderr,"EL_REMOTE_RATE_MODE Setting Fail..\n");
			return false;
		}
	}
	return true;
}