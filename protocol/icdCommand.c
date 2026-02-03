/**
 * @file icdCommand.c
 * @brief ICD 명령 처리 함수 구현부
 *
 * 본 파일은 keepAlive(), iBit() 명령 처리에 대한
 * 실제 실행 로직을 담당한다.
 */

#include "icdCommand.h"
#include "cmdRegistry.h"
#include <stdio.h>
#include <string.h>


double endianChange(char* i_chData)
{
	int i;
	double dValue;
	char chChangeEndian[8];
	for(i=0; i<8; i++){
//		fprintf(stderr,"%02X ",i_chData[i]);
		chChangeEndian[7-i] = i_chData[i];
	}
	memset(&dValue, 0x0, sizeof(double));
	memcpy(&dValue, chChangeEndian, sizeof(double));
	return dValue;
}



uint64_t swap_uint64(uint64_t val) {
    return ((val << 56) & 0xFF00000000000000ULL) |
           ((val << 40) & 0x00FF000000000000ULL) |
           ((val << 24) & 0x0000FF0000000000ULL) |
           ((val <<  8) & 0x000000FF00000000ULL) |
           ((val >>  8) & 0x00000000FF000000ULL) |
           ((val >> 24) & 0x0000000000FF0000ULL) |
           ((val >> 40) & 0x000000000000FF00ULL) |
           ((val >> 56) & 0x00000000000000FFULL);
}

// double 타입 데이터를 little endian과 big endian 간에 변환
double swap_double(double val) {
    uint64_t temp;
    double result;
    // double 값을 uint64_t로 안전하게 복사
    memcpy(&temp, &val, sizeof(double));
    // 바이트 순서를 변환
    temp = swap_uint64(temp);
    // 변환된 값을 다시 double로 복사
    memcpy(&result, &temp, sizeof(double));
    return result;
}

int buildForwardReqIdInfo(const void* pvUserData, void* pvOutData)
{
	(void)pvUserData;
	REQ_ID *pstReqId = (REQ_ID *)pvOutData;
	pstReqId->chTmp = 0x01;
	fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
	return sizeof(REQ_ID);
}
int dispatchCmdIdInfo(const void* pvRecvData, void* pvOutData)
{
	(void)pvRecvData;
	(void)pvOutData;
	fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
	return sizeof(RES_ID);
}
int buildResIdInfo(const void* pvUserData, void* pvOutData)
{
	RES_ID *pstUserData	= (RES_ID *)(pvUserData);
	RES_ID *pstResId 	= (RES_ID *)(pvOutData);
	pstResId->chResult 	= pstUserData->chResult;
	fprintf(stderr, "RES_ID %04X\n", pstResId->chResult);
	return sizeof(RES_ID);
}

int buildForwardReqKeepAlive(const void* pvUserData, void* pvOutData)
{    
	(void)pvUserData;
	REQ_KEEP_ALIVE *pstReqKeepalive = (REQ_KEEP_ALIVE *)pvOutData;
	pstReqKeepalive->chTmp = 0x01;
    return sizeof(REQ_KEEP_ALIVE);
}
int dispatchCmdKeepAlive(const void* pvRecvData, void* pvOutData)
{
	(void)pvRecvData;
	RES_KEEP_ALIVE* pstResKeepalive = (RES_KEEP_ALIVE *)pvOutData;
    pstResKeepalive->chResult = 0x01;
    printf("[RES] KEEP_ALIVE status=%u\n", pstResKeepalive->chResult);
    return sizeof(RES_KEEP_ALIVE);
}
int buildResKeepAlive(const void* pvUserData, void* pvOutData)
{
	RES_KEEP_ALIVE *pstUserData	= (RES_KEEP_ALIVE *)(pvUserData);
	RES_KEEP_ALIVE *pstResId 	= (RES_KEEP_ALIVE *)(pvOutData);
	pstResId->chResult = pstUserData->chResult;
	return sizeof(RES_KEEP_ALIVE);
}

int buildForwardReqIbit(const void* pvUserData, void* pvOutData)
{    
	(void)pvUserData;
	REQ_BIT *pstReqBit = (REQ_BIT *)pvOutData;
	pstReqBit->chBit = 0x01;
    return sizeof(REQ_BIT);
}
int dispatchCmdIbit(const void* pvRecvData, void* pvOutData)
{
	(void)pvRecvData;
	RES_BIT *pstResIbit = (RES_BIT *)(pvOutData);
	pstResIbit->chBitTotResult    = 0x01;
	pstResIbit->chPositionResult  = 0x01;
	fprintf(stderr, "ICD_IBIT executed\n");
	return sizeof(RES_BIT);
}
int buildResIbit(const void* pvUserData, void* pvOutData)
{
	RES_BIT *pstUserData		= (RES_BIT *)(pvUserData);
	RES_BIT *pstResId 			= (RES_BIT *)(pvOutData);
	pstResId->chBitTotResult 	= pstUserData->chBitTotResult;
	pstResId->chPositionResult	= pstUserData->chPositionResult;
	return sizeof(RES_BIT);
}

int buildForwardReqRbit(const void* pvUserData, void* pvOutData)
{    
	(void)pvUserData;
	REQ_BIT *pstReqBit = (REQ_BIT *)pvOutData;
	pstReqBit->chBit = 0x01;
    return sizeof(REQ_BIT);
}
int dispatchCmdRbit(const void* pvRecvData, void* pvOutData)
{
	(void)pvRecvData;
	RES_BIT *pstResIbit = (RES_BIT *)(pvOutData);
	pstResIbit->chBitTotResult    = 0x01;
	pstResIbit->chPositionResult  = 0x01;
	fprintf(stderr, "ICD_RBIT executed\n");
	return sizeof(RES_BIT);
}
int buildResRbit(const void* pvUserData, void* pvOutData)
{
	RES_BIT *pstUserData		= (RES_BIT *)(pvUserData);
	RES_BIT *pstResId 			= (RES_BIT *)(pvOutData);
	pstResId->chBitTotResult 	= pstUserData->chBitTotResult;
	pstResId->chPositionResult	= pstUserData->chPositionResult;
	return sizeof(RES_BIT);
}

int buildForwardReqCbit(const void* pvUserData, void* pvOutData)
{    
	(void)pvUserData;
	REQ_BIT *pstReqBit = (REQ_BIT *)pvOutData;
	pstReqBit->chBit = 0x01;
    return sizeof(REQ_BIT);
}
int dispatchCmdCbit(const void* pvRecvData, void* pvOutData)
{
	(void)pvRecvData;
	RES_BIT *pstResIbit = (RES_BIT *)(pvOutData);
	pstResIbit->chBitTotResult    = 0x01;
	pstResIbit->chPositionResult  = 0x01;
	fprintf(stderr, "ICD_RBIT executed\n");
	return sizeof(RES_BIT);
}
int buildResCbit(const void* pvUserData, void* pvOutData)
{
	RES_BIT *pstUserData		= (RES_BIT *)(pvUserData);
	RES_BIT *pstResId 			= (RES_BIT *)(pvOutData);
	pstResId->chBitTotResult 	= pstUserData->chBitTotResult;
	pstResId->chPositionResult	= pstUserData->chPositionResult;
	return sizeof(RES_BIT);
}

int buildForwardReqPositionAzElSet(const void* pvUserData, void* pvOutData)
{    
	REQ_POSITIONER_AZ_EL_SET *pstReqUserData	= (REQ_POSITIONER_AZ_EL_SET *)pvUserData;
	REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet 	= (REQ_POSITIONER_AZ_EL_SET *)pvOutData;
	memcpy(pstReqAzElSet->chAzimuthDeg,  	pstReqUserData->chAzimuthDeg,	sizeof(pstReqAzElSet->chAzimuthDeg));
	memcpy(pstReqAzElSet->chElevationDeg,  	pstReqUserData->chElevationDeg,	sizeof(pstReqAzElSet->chElevationDeg));
    return sizeof(REQ_POSITIONER_AZ_EL_SET);
}
int dispatchCmdPositionAzElSet(const void* pvRecvData, void* pvOutData)
{
	REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet 	= (REQ_POSITIONER_AZ_EL_SET *)(pvRecvData);
	REQ_POSITIONER_AZ_EL_SET *pstReqUserData	= (REQ_POSITIONER_AZ_EL_SET *)(pvOutData);
	memcpy(pstReqUserData->chAzimuthDeg,  	pstReqAzElSet->chAzimuthDeg, 	sizeof(pstReqAzElSet->chAzimuthDeg));
	memcpy(pstReqUserData->chElevationDeg,  pstReqAzElSet->chElevationDeg, 	sizeof(pstReqAzElSet->chElevationDeg));
	fprintf(stderr, "POSITIONER_AZ_EL_SET executed\n");
	return sizeof(REQ_POSITIONER_AZ_EL_SET);
}
int buildResPositionAzElSet(const void* pvUserData, void* pvOutData)
{
	RES_POSITIONER_AZ_EL_SET *pstUserData	= (RES_POSITIONER_AZ_EL_SET *)pvUserData;
	RES_POSITIONER_AZ_EL_SET *pstResAzElSet	= (RES_POSITIONER_AZ_EL_SET *)pvOutData;
	pstResAzElSet->chResult 				= pstUserData->chResult;	
	return sizeof(RES_POSITIONER_AZ_EL_SET);
}


int buildForwardReqTrackingSelect(const void* pvUserData, void* pvOutData)
{    
	REQ_TRACKING_SELECT *pstReqUserData 		= (REQ_TRACKING_SELECT *)pvUserData;
	REQ_TRACKING_SELECT *pstReqTrackingSelect	= (REQ_TRACKING_SELECT *)pvOutData;
	pstReqTrackingSelect->chTrackingSelect 		= pstReqUserData->chTrackingSelect;
    return sizeof(REQ_TRACKING_SELECT);
}
int dispatchCmdTrackingSelect(const void* pvRecvData, void* pvOutData)
{
	REQ_TRACKING_SELECT *pstReqAzElSet 	= (REQ_TRACKING_SELECT *)pvRecvData;
	REQ_TRACKING_SELECT *pstReqUserData	= (REQ_TRACKING_SELECT *)pvOutData;
	pstReqUserData->chTrackingSelect = pstReqAzElSet->chTrackingSelect;
	if(pstReqAzElSet->chTrackingSelect == SELF_TRACKING){
		fprintf(stderr,"SELF_TRACKING\n");
	}else if(pstReqAzElSet->chTrackingSelect == EXTERNAL_DEV_TRACKING){
		fprintf(stderr,"EXTERNAL_DEV_TRACKING\n");
	}else{
		fprintf(stderr,"Tracking Select Fail\n");
	}
	
	fprintf(stderr, "Tracking Select Setting executed\n");
	return sizeof(REQ_TRACKING_SELECT);
}
int buildResTrackingSelect(const void* pvUserData, void* pvOutData)
{
	RES_TRACKING_SELECT *pstUserData			= (RES_TRACKING_SELECT *)(pvUserData);
	RES_TRACKING_SELECT *pstResTrackingSelect	= (RES_TRACKING_SELECT *)(pvOutData);
	pstResTrackingSelect->chResult 				= pstUserData->chResult;	
	return sizeof(RES_TRACKING_SELECT);
}


int buildForwardReqAcuModeSelect(const void* pvUserData, void* pvOutData)
{    
	REQ_ACU_MODE *pstReqUserData 	= (REQ_ACU_MODE *)pvUserData;
	REQ_ACU_MODE *pstReqAcuMode		= (REQ_ACU_MODE *)pvOutData;
	pstReqAcuMode->chAcuMode 		= pstReqUserData->chAcuMode;
    return sizeof(REQ_ACU_MODE);
}
int dispatchCmdAcuModeSelect(const void* pvRecvData, void* pvOutData)
{
	REQ_ACU_MODE *pstReqAcuMode 	= (REQ_ACU_MODE *)pvRecvData;
	REQ_ACU_MODE *pstReqUserData	= (REQ_ACU_MODE *)pvOutData;
	pstReqUserData->chAcuMode 		= pstReqAcuMode->chAcuMode;
	if(pstReqAcuMode->chAcuMode == RATE){
		fprintf(stderr,"ACU Mode is RATE\n");
	}else if(pstReqAcuMode->chAcuMode == POSITION){
		fprintf(stderr,"ACU Mode is POSITION\n");
	}else{
		fprintf(stderr,"ACU Mode Select Fail\n");
	}
	
	fprintf(stderr, "Tracking Select Setting executed\n");
	return sizeof(REQ_ACU_MODE);
}
int buildResAcuModeSelect(const void* pvUserData, void* pvOutData)
{
	RES_ACU_MODE *pstUserData		= (RES_ACU_MODE *)(pvUserData);
	RES_ACU_MODE *pstResAcuMode		= (RES_ACU_MODE *)(pvOutData);
	pstResAcuMode->chResult 		= pstUserData->chResult;	
	return sizeof(RES_ACU_MODE);
}


int buildForwardReqAutoTrackingWait(const void* pvUserData, void* pvOutData)
{    
	REQ_AUTO_TRACKING_WAIT *pstReqUserData			= (REQ_AUTO_TRACKING_WAIT *)pvUserData;
	REQ_AUTO_TRACKING_WAIT *pstReqAutoTrackingWait	= (REQ_AUTO_TRACKING_WAIT *)pvOutData;
	pstReqAutoTrackingWait->chWaitOnOff 			= pstReqUserData->chWaitOnOff;
	pstReqAutoTrackingWait->dStandbyAz 				= pstReqUserData->dStandbyAz;
	pstReqAutoTrackingWait->dStandbyEl 				= pstReqUserData->dStandbyEl;
    return sizeof(REQ_AUTO_TRACKING_WAIT);
}
int dispatchCmdAutoTrackingWait(const void* pvRecvData, void* pvOutData)
{
	REQ_AUTO_TRACKING_WAIT *pstReqAutoTrackingWait	= (REQ_AUTO_TRACKING_WAIT *)pvRecvData;
	REQ_AUTO_TRACKING_WAIT *pstReqUserData			= (REQ_AUTO_TRACKING_WAIT *)pvOutData;
	
	pstReqUserData->chWaitOnOff 					= pstReqAutoTrackingWait->chWaitOnOff;
	pstReqUserData->dStandbyAz 						= swap_double(pstReqAutoTrackingWait->dStandbyAz);
	pstReqUserData->dStandbyEl 						= swap_double(pstReqAutoTrackingWait->dStandbyEl);
	if(pstReqUserData->chWaitOnOff == AUTO_TRACKING_ON){
		fprintf(stderr,"Auto Tracking On\n");
		fprintf(stderr,"Standby Az is %lf, El is %lf\n", pstReqUserData->dStandbyAz, pstReqUserData->dStandbyEl);
	}else {
		fprintf(stderr,"Auto Tracking Off\n");
	}
	return sizeof(REQ_AUTO_TRACKING_WAIT);
}
int buildResAutoTrackingWait(const void* pvUserData, void* pvOutData)
{
	RES_AUTO_TRACKING_WAIT *pstUserData				= (RES_AUTO_TRACKING_WAIT *)(pvUserData);
	RES_AUTO_TRACKING_WAIT *pstResAutoTrackingWait	= (RES_AUTO_TRACKING_WAIT *)(pvOutData);
	pstResAutoTrackingWait->chResult 				= pstUserData->chResult;	
	return sizeof(RES_AUTO_TRACKING_WAIT);
}

int buildResImuData(const void* pvUserData, void* pvOutData)
{
	RES_RPY_DATA *pstUserData		= (RES_RPY_DATA *)(pvUserData);
	RES_RPY_DATA *pstResRpyData 	= (RES_RPY_DATA *)(pvOutData);
	pstResRpyData->dRoll 			= pstUserData->dRoll;
	pstResRpyData->dPitch 			= pstUserData->dPitch;
	pstResRpyData->dYaw 			= pstUserData->dYaw;
	return sizeof(RES_RPY_DATA);
}


int buildResGpsData(const void* pvUserData, void* pvOutData)
{
	RES_LLA_DATA *pstUserData		= (RES_LLA_DATA *)(pvUserData);
	RES_LLA_DATA *pstResGpsData 	= (RES_LLA_DATA *)(pvOutData);
	pstResGpsData->dLatitude 		= pstUserData->dLatitude;
	pstResGpsData->dLongitude 		= pstUserData->dLongitude;
	pstResGpsData->dAltitude 		= pstUserData->dAltitude;
	return sizeof(RES_LLA_DATA);
}

int buildResCtrlAzElData(const void* pvUserData, void* pvOutData)
{
	RES_AZ_EL_DATA *pstUserData			= (RES_AZ_EL_DATA *)(pvUserData);
	RES_AZ_EL_DATA *pstResCtrlAzElData	= (RES_AZ_EL_DATA *)(pvOutData);
	pstResCtrlAzElData->dAz 				= pstUserData->dAz;
	pstResCtrlAzElData->dEl 				= pstUserData->dEl;
	return sizeof(RES_AZ_EL_DATA);
}

int buildResSpData(const void* pvUserData, void* pvOutData)
{
	RES_AZ_EL_DATA *pstUserData		= (RES_AZ_EL_DATA *)(pvUserData);
	RES_AZ_EL_DATA *pstResSpData	= (RES_AZ_EL_DATA *)(pvOutData);
	pstResSpData->dAz 				= pstUserData->dAz;
	pstResSpData->dEl 				= pstUserData->dEl;
	return sizeof(RES_AZ_EL_DATA);
}

int buildResExternData(const void* pvUserData, void* pvOutData)
{
	RES_LLA_DATA *pstUserData			= (RES_LLA_DATA *)(pvUserData);
	RES_LLA_DATA *pstResExternData		= (RES_LLA_DATA *)(pvOutData);
	pstResExternData->dLatitude 		= pstUserData->dLatitude;
	pstResExternData->dLongitude 		= pstUserData->dLongitude;
	pstResExternData->dAltitude			= pstUserData->dAltitude;
	return sizeof(RES_AZ_EL_DATA);
}


int dispatchKeyboardData(const void* pvRecvData, void* pvOutData)
{
	REQ_KEYBOARD_DATA *pstReqKeyboard 	= (REQ_KEYBOARD_DATA *)pvRecvData;
	REQ_KEYBOARD_DATA *pstReqUserData	= (REQ_KEYBOARD_DATA *)pvOutData;
	pstReqUserData->dAz 		= pstReqKeyboard->dAz;
	pstReqUserData->dEl 		= pstReqKeyboard->dEl;	
	fprintf(stderr, "Recv Keyboard Data\n");
	return sizeof(REQ_KEYBOARD_DATA);
}
int buildResKeyboardData(const void* pvUserData, void* pvOutData)
{
	RES_AZ_EL_DATA *pstUserData			= (RES_AZ_EL_DATA *)(pvUserData);
	RES_AZ_EL_DATA *pstResKeyboardData	= (RES_AZ_EL_DATA *)(pvOutData);
	pstResKeyboardData->dAz 			= pstUserData->dAz;
	pstResKeyboardData->dEl 			= pstUserData->dEl;
	return sizeof(RES_AZ_EL_DATA);
}