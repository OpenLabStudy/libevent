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

int moveAzElPosition(double i_dAz, double i_dEl, char* pchOutData);
int moveAzElRate(double i_dAz, double i_dEl, char* pchOutData);
int moveAzRate(int i_iAcuFd, double i_dAz, char* pchOutData);
int moveElRate(int i_iAcuFd, double i_dEl, char* pchOutData);
int splitAcuDataString(char* chpStringData, char chSeparate, char** chppStorage, int iMaxSplitCount);
int modeChange(char i_chMode, char* pchOutData);
int readAzElFromAcu(char* pchOutData);


#ifdef __cplusplus
}
#endif

#endif /* SRC_ACUUTILS_H_ */