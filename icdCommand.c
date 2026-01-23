/**
 * @file icdCommand.c
 * @brief ICD 명령 처리 함수 구현부
 *
 * 본 파일은 keepAlive(), iBit() 명령 처리에 대한
 * 실제 실행 로직을 담당한다.
 */

#include "icdCommand.h"
#include "frame.h"
#include <stdio.h>
#include <string.h>

double endianChange(char* i_chData)
{
	int i;
	double dValue=0.0;
	char chChangeEndian[8];
	fprintf(stderr,"### endianChange() ###\n");
	for(i=0; i<8; i++){
		fprintf(stderr,"i_chData[%d]: %02X\n", i, (unsigned char)i_chData[i]);
		chChangeEndian[7-i] = i_chData[i];
	}	
	memcpy((void*)&dValue, chChangeEndian, sizeof(double));
	fprintf(stderr,"### Changed Endian Value: %lf ###\n", dValue);
	return dValue;
}

void endianChange1(double dValue, char* pchData)
{
	int i;
	char chChangeEndian[8];
	memcpy(chChangeEndian, (void*)&dValue, sizeof(double));
	fprintf(stderr,"### endianChange() ###\n");
	for(i=0; i<8; i++){
		pchData[7-i] = chChangeEndian[i];
		fprintf(stderr,"pchData[%d]: %02X\n", i, (unsigned char)chChangeEndian[i]);
	}	
}

double swapDouble(char* i_chData)
{
    uint64_t x;
    memcpy(&x, i_chData, 8);

    x = ((x & 0x00000000000000FFULL) << 56) |
        ((x & 0x000000000000FF00ULL) << 40) |
        ((x & 0x0000000000FF0000ULL) << 24) |
        ((x & 0x00000000FF000000ULL) << 8 ) |
        ((x & 0x000000FF00000000ULL) >> 8 ) |
        ((x & 0x0000FF0000000000ULL) >> 24) |
        ((x & 0x00FF000000000000ULL) >> 40) |
        ((x & 0xFF00000000000000ULL) >> 56);

    double out;
    memcpy(&out, &x, 8);
    return out;
}

int reqIDInfo(void* pvCmdData, void* pvUserData, void* pvOutData)
{
	REQ_ID stReqId = { .chTmp = 1};
	memcpy(pvOutData, &stReqId, sizeof(stReqId));
	return sizeof(RES_ID);
}

int resIDInfo(const void* pvRecvData, void* pvUserData, void* pvOutData)
{
	REQ_ID *pstReqId = (REQ_ID *)(pvRecvData);
	RES_ID *pstResId = (RES_ID *)(pvUserData);
	RES_ID *pstOutResId = (RES_ID *)(pvOutData);
	pstOutResId->chResult = pstResId->chResult;
	fprintf(stderr, "RES_ID %04X\n", pstResId->chResult);
	return sizeof(RES_ID);
}

int resKeepAlive(const void* pvRecvData, void* pvUserData, void* pvOutData)
{
    RES_KEEP_ALIVE* pstResKeepalive = (RES_KEEP_ALIVE *)pvOutData;
    pstResKeepalive->chResult = 0x01;
    printf("[RES] KEEP_ALIVE status=%u\n", pstResKeepalive->chResult);
    return sizeof(RES_KEEP_ALIVE);
}

int resIBit(const void* pvRecvData, void* pvUserData, void* pvOutData)
{
	(void)pvRecvData;

	RES_BIT *pstResIBit = (RES_BIT *)(pvOutData);

	pstResIBit->chBitTotResult    = 0x01;
	pstResIBit->chPositionResult  = 0x01;

	fprintf(stderr, "ICD_IBIT executed\n");
	return sizeof(RES_BIT);
}

int resRBit(const void* pvRecvData, void* pvUserData, void* pvOutData)
{
	(void)pvRecvData;

	RES_BIT *pstResIBit = (RES_BIT *)(pvOutData);

	pstResIBit->chBitTotResult    = 0x01;
	pstResIBit->chPositionResult  = 0x01;

	fprintf(stderr, "ICD_RBIT executed\n");
	return sizeof(RES_BIT);
}

int resCBit(const void* pvRecvData, void* pvUserData, void* pvOutData)
{
	(void)pvRecvData;

	RES_BIT *pstResIBit = (RES_BIT *)(pvOutData);

	pstResIBit->chBitTotResult    = 0x01;
	pstResIBit->chPositionResult  = 0x01;

	fprintf(stderr, "ICD_CBIT executed\n");
	return sizeof(RES_BIT);
}

int resPositionAzElSet(const void* pvRecvData, void* pvUserData, void* pvOutData)
{
	double dAz, dEl;
	REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet = (REQ_POSITIONER_AZ_EL_SET *)(pvRecvData + sizeof(FRAME_HEADER));
	RES_POSITIONER_AZ_EL_SET *pstResAzElSet = (RES_POSITIONER_AZ_EL_SET *)(pvOutData);
	dAz = endianChange(pstReqAzElSet->chAzimuthDeg);
	dEl = endianChange(pstReqAzElSet->chElevationDeg);	
	//todo Az,El설정에 대한 명령 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	fprintf(stderr,"AZ:%.03lf, EL:%.03lf\n", dAz, dEl);
	pstResAzElSet->chResult = 0x01;

	fprintf(stderr, "POSITION AZ EL Setting executed\n");
	return sizeof(RES_POSITIONER_AZ_EL_SET);
}

int resTrackingSelect(const void* pvRecvData, void* pvUserData, void* pvOutData)
{
	REQ_TRACKING_SELECT *pstReqTrackingSelect = (REQ_TRACKING_SELECT *)(pvRecvData + sizeof(FRAME_HEADER));
	RES_TRACKING_SELECT *pstResTrackingSelect = (RES_TRACKING_SELECT *)(pvOutData);

	pstResTrackingSelect->chResult = 0x01;
	if(pstReqTrackingSelect->chTrackingSelect == SELF_TRACKING){
		fprintf(stderr,"SELF_TRACKING\n");
	}else if(pstReqTrackingSelect->chTrackingSelect == EXTERNAL_DEV_TRACKING){
		fprintf(stderr,"EXTERNAL_DEV_TRACKING\n");
	}else{
		fprintf(stderr,"Tracking Select Fail\n");
		pstResTrackingSelect->chResult = 0x0;
	}
	
	fprintf(stderr, "Tracking Select Setting executed\n");
	return sizeof(RES_TRACKING_SELECT);
}

int trackingStartPointSet(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	// REQ_TRACKING_START_POINT_SET *pstReqTrackingStartPointSet = (REQ_TRACKING_START_POINT_SET *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_TRACKING_START_POINT_SET *pstResTrackingStartPointSet = (RES_TRACKING_START_POINT_SET *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResTrackingStartPointSet->chResult = 0x01;

	fprintf(stderr, "Tracking Start point Setting executed\n");
	return sizeof(RES_TRACKING_START_POINT_SET);
}

int cannonBallTrajectoryInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	// REQ_CANNON_BALL_TRAJECTORY_INFO *pstReqCannonBallTrajectoryInfo = (REQ_CANNON_BALL_TRAJECTORY_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_CANNON_BALL_TRAJECTORY_INFO *pstResCannonBallTrajectoryInfo = (RES_CANNON_BALL_TRAJECTORY_INFO *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResCannonBallTrajectoryInfo->chResult = 0x01;

	fprintf(stderr, "Connon Ball Tracjectory Info executed\n");
	return sizeof(RES_CANNON_BALL_TRAJECTORY_INFO);
}

int shelterCoordinateInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	// REQ_SHELTER_COORDINATE_INFO *pstReqShelterCoordinateInfo = (REQ_SHELTER_COORDINATE_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_SHELTER_COORDINATE_INFO *pstResShelterCoordinateInfo = (RES_SHELTER_COORDINATE_INFO *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResShelterCoordinateInfo->chResult = 0x01;

	fprintf(stderr, "Shelter Coordinate Info executed\n");
	return sizeof(RES_SHELTER_COORDINATE_INFO);
}

int mccCoordinateInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	// REQ_EXTERN_DEV_COORDINATE_INFO *pstReqExternDevCoordinateInfo = (REQ_EXTERN_DEV_COORDINATE_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_EXTERN_DEV_COORDINATE_INFO *pstResExternDevCoordinateInfo = (RES_EXTERN_DEV_COORDINATE_INFO *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResExternDevCoordinateInfo->chResult = 0x01;

	fprintf(stderr, "Extern Coordinate Info executed\n");
	return sizeof(RES_EXTERN_DEV_COORDINATE_INFO);
}

int cannonCoordinateInifo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	// REQ_CANNON_COORDINATE_INFO *pstReqCannonCoordinateInfo = (REQ_CANNON_COORDINATE_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_CANNON_COORDINATE_INFO *pstResCannonCoordinateInfo = (RES_CANNON_COORDINATE_INFO *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResCannonCoordinateInfo->chResult = 0x01;

	fprintf(stderr, "Cannon Coordinate Info executed\n");
	return sizeof(RES_CANNON_COORDINATE_INFO);
}

int trackingControl(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_TRACKING_CONTROL *pstReqTrackingControl = (REQ_TRACKING_CONTROL *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_TRACKING_CONTROL *pstResTrackingControl = (RES_TRACKING_CONTROL *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResTrackingControl->chResult = 0x01;
	if(pstReqTrackingControl->chStartStop == TRACKING_STOP){
		fprintf(stderr, "TRACKING_STOP\n");
	}else if(pstReqTrackingControl->chStartStop == TRACKING_START){
		fprintf(stderr, "TRACKING_START\n");
	}else{
		fprintf(stderr,"Tracking Control Fail\n");
		pstResTrackingControl->chResult = 0x00;
	}

	fprintf(stderr, "Tracking Control executed\n");
	return sizeof(RES_TRACKING_CONTROL);
}

int positionDegTransferCtrl(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_POSITIONER_DEG_SEND *pstReqPositionDegSend = (REQ_POSITIONER_DEG_SEND *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_POSITIONER_DEG_SEND *pstResPositionDegSend = (RES_POSITIONER_DEG_SEND *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResPositionDegSend->chResult = 0x01;
	if(pstReqPositionDegSend->chSendOnOff == AZ_EL_SEND_OFF){
		fprintf(stderr,"AZ_EL_SEND_OFF\n");
	}else if(pstReqPositionDegSend->chSendOnOff == AZ_EL_SEND_ON){
		fprintf(stderr,"AZ_EL_SEND_ON\n");
	}else{
		fprintf(stderr,"Position Degree Transfer Control Fail\n");
		pstResPositionDegSend->chResult = 0x00;
	}

	fprintf(stderr, "Position Degree Send executed\n");
	return sizeof(RES_POSITIONER_DEG_SEND);
}

int acuModeSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_ACU_MODE *pstReqAcuMode = (REQ_ACU_MODE *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_ACU_MODE *pstResAcuMode = (RES_ACU_MODE *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResAcuMode->chResult = 0x01;
	if(pstReqAcuMode->chAcuMode == RATE){
		fprintf(stderr,"ACU Mode is RATE\n");
	}else if(pstReqAcuMode->chAcuMode == POSITION){
		fprintf(stderr,"ACU Mode is RATE\n");
	}else{
		fprintf(stderr,"ACU Mode Select Fail\n");
		pstResAcuMode->chResult = 0x00;
	}

	fprintf(stderr, "ACU Mode Select executed\n");
	return sizeof(RES_ACU_MODE);
}

int timeSynqCheck(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	// REQ_TIME_SYNQ_CHECK *pstReqTimeSynqCheck = (REQ_TIME_SYNQ_CHECK *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_TIME_SYNQ_CHECK *pstResTimeSynqCheck = (RES_TIME_SYNQ_CHECK *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResTimeSynqCheck->chResult = 0x01;

	fprintf(stderr, "Time Synq Check executed\n");
	return sizeof(RES_TIME_SYNQ_CHECK);
}

int timeSynqSet(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	// REQ_TIME_SYNQ_SET *pstReqTimeSynqSet = (REQ_TIME_SYNQ_SET *)(puchRecvData + sizeof(FRAME_HEADER));
	// RES_TIME_SYNQ_SET *pstResTimeSynqSet = (RES_TIME_SYNQ_SET *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	// pstResTimeSynqSet->chResult = 0x01;

	fprintf(stderr, "Time Synq Set executed\n");
	return sizeof(RES_TIME_SYNQ_SET);
}

int azElOffset(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_AZ_EL_OFFSET_SET *pstReqAzElOffsetSet = (REQ_AZ_EL_OFFSET_SET *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_AZ_EL_OFFSET_SET *pstResAzElOffsetSet = (RES_AZ_EL_OFFSET_SET *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResAzElOffsetSet->chResult = 0x01;
	fprintf(stderr,"AZ Offset is %d, EL Offset is %d\n", pstReqAzElOffsetSet->iAzOffset, pstReqAzElOffsetSet->iElOffset);
	
	fprintf(stderr, "AZ EL Offset Set executed\n");
	return sizeof(RES_AZ_EL_OFFSET_SET);
}



int setAutoTrackingWati(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_AUTO_TRACKING_WAIT *pstReqAutoTrackingWait = (REQ_AUTO_TRACKING_WAIT *)(puchRecvData + sizeof(FRAME_HEADER));
	fprintf(stderr,"Auto Tracking Wait %s\n", pstReqAutoTrackingWait->chWaitOnOff == 0x01 ? "ON" : "OFF");
	fprintf(stderr,"Standby AZ : %lf, EL : %lf\n", pstReqAutoTrackingWait->dStandbyAz, pstReqAutoTrackingWait->dStandbyEl);
	return sizeof(RES_AUTO_TRACKING_WAIT);
}