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

double endianChange(char* i_chData)
{
	int i;
	double dValue;
	char chChangeEndian[8];
	for(i=0; i<8; i++){
		chChangeEndian[7-i] = i_chData[i];
	}
	memset(&dValue, 0x0, sizeof(double));
	memcpy(&dValue, chChangeEndian, sizeof(double));
	return dValue;
}

int idInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	RES_ID *pstResId = (RES_ID *)(puchRecvData);

	fprintf(stderr, "RES_ID %04X\n", pstResId->chResult);
	return sizeof(RES_ID);
}

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

	RES_KEEP_ALIVE *pstResKeepAlive = (RES_KEEP_ALIVE *)(puchCmdResult);

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

	RES_BIT *pstResIBit = (RES_BIT *)(puchCmdResult);

	pstResIBit->chBitTotResult    = 0x01;
	pstResIBit->chPositionResult  = 0x01;

	fprintf(stderr, "ICD_IBIT executed\n");
	return sizeof(RES_BIT);
}

int rBit(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	(void)puchRecvData;

	RES_BIT *pstResIBit = (RES_BIT *)(puchCmdResult);

	pstResIBit->chBitTotResult    = 0x01;
	pstResIBit->chPositionResult  = 0x01;

	fprintf(stderr, "ICD_RBIT executed\n");
	return sizeof(RES_BIT);
}

int cBit(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	(void)puchRecvData;

	RES_BIT *pstResIBit = (RES_BIT *)(puchCmdResult);

	pstResIBit->chBitTotResult    = 0x01;
	pstResIBit->chPositionResult  = 0x01;

	fprintf(stderr, "ICD_CBIT executed\n");
	return sizeof(RES_BIT);
}

int positionAzElSet(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	double dSetAz, dSetEl;
	REQ_POSITIONER_AZ_EL_SET *pstReqAzElSet = (REQ_POSITIONER_AZ_EL_SET *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_POSITIONER_AZ_EL_SET *pstResAzElSet = (RES_POSITIONER_AZ_EL_SET *)(puchCmdResult);
	dSetAz = endianChange(pstReqAzElSet->chAzimuthDeg);
	dSetEl = endianChange(pstReqAzElSet->chElevationDeg);
	//todo Az,El설정에 대한 명령 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResAzElSet->chResult = 0x01;

	fprintf(stderr, "POSITION AZ EL Setting executed\n");
	return sizeof(RES_POSITIONER_AZ_EL_SET);
}

int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_TRACKING_SELECT *pstReqTrackingSelect = (REQ_TRACKING_SELECT *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_TRACKING_SELECT *pstResTrackingSelect = (RES_TRACKING_SELECT *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResTrackingSelect->chResult = 0x01;

	fprintf(stderr, "Tracking Select Setting executed\n");
	return sizeof(RES_TRACKING_SELECT);
}

int trackingStartPointSet(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_TRACKING_START_POINT_SET *pstReqTrackingStartPointSet = (REQ_TRACKING_START_POINT_SET *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_TRACKING_START_POINT_SET *pstResTrackingStartPointSet = (RES_TRACKING_START_POINT_SET *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResTrackingStartPointSet->chResult = 0x01;

	fprintf(stderr, "Tracking Start point Setting executed\n");
	return sizeof(RES_TRACKING_START_POINT_SET);
}

int cannonBallTrajectoryInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_CANNON_BALL_TRAJECTORY_INFO *pstReqCannonBallTrajectoryInfo = (REQ_CANNON_BALL_TRAJECTORY_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_CANNON_BALL_TRAJECTORY_INFO *pstResCannonBallTrajectoryInfo = (RES_CANNON_BALL_TRAJECTORY_INFO *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResCannonBallTrajectoryInfo->chResult = 0x01;

	fprintf(stderr, "Connon Ball Tracjectory Info executed\n");
	return sizeof(RES_CANNON_BALL_TRAJECTORY_INFO);
}

int shelterCoordinateInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_SHELTER_COORDINATE_INFO *pstReqShelterCoordinateInfo = (REQ_SHELTER_COORDINATE_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_SHELTER_COORDINATE_INFO *pstResShelterCoordinateInfo = (RES_SHELTER_COORDINATE_INFO *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResShelterCoordinateInfo->chResult = 0x01;

	fprintf(stderr, "Shelter Coordinate Info executed\n");
	return sizeof(RES_SHELTER_COORDINATE_INFO);
}

int mccCoordinateInfo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_EXTERN_DEV_COORDINATE_INFO *pstReqExternDevCoordinateInfo = (REQ_EXTERN_DEV_COORDINATE_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_EXTERN_DEV_COORDINATE_INFO *pstResExternDevCoordinateInfo = (RES_EXTERN_DEV_COORDINATE_INFO *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResExternDevCoordinateInfo->chResult = 0x01;

	fprintf(stderr, "Extern Coordinate Info executed\n");
	return sizeof(RES_EXTERN_DEV_COORDINATE_INFO);
}

int cannonCoordinateInifo(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_CANNON_COORDINATE_INFO *pstReqCannonCoordinateInfo = (REQ_CANNON_COORDINATE_INFO *)(puchRecvData + sizeof(FRAME_HEADER));
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

	fprintf(stderr, "Tracking Control executed\n");
	return sizeof(RES_TRACKING_CONTROL);
}

int positionDegCtrl(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_POSITIONER_DEG_SEND *pstReqPositionDegSend = (REQ_POSITIONER_DEG_SEND *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_POSITIONER_DEG_SEND *pstResPositionDegSend = (RES_POSITIONER_DEG_SEND *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResPositionDegSend->chResult = 0x01;

	fprintf(stderr, "Position Degree Send executed\n");
	return sizeof(RES_POSITIONER_DEG_SEND);
}

int acuModeSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_ACU_MODE *pstReqAcuMode = (REQ_ACU_MODE *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_ACU_MODE *pstResAcuMode = (RES_ACU_MODE *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResAcuMode->chResult = 0x01;

	fprintf(stderr, "ACU Mode Select executed\n");
	return sizeof(RES_ACU_MODE);
}

int timeSynqCheck(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_TIME_SYNQ_CHECK *pstReqTimeSynqCheck = (REQ_TIME_SYNQ_CHECK *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_TIME_SYNQ_CHECK *pstResTimeSynqCheck = (RES_TIME_SYNQ_CHECK *)(puchCmdResult);
	
	//todo 명령설정에 대한 처리 결과는 각 UDS의 응답으로 최종 처리되어야함.
	pstResTimeSynqCheck->chResult = 0x01;

	fprintf(stderr, "Time Synq Check executed\n");
	return sizeof(RES_TIME_SYNQ_CHECK);
}

int timeSynqSet(unsigned char* puchRecvData, unsigned char* puchCmdResult)
{
	REQ_TIME_SYNQ_SET *pstReqTimeSynqSet = (REQ_TIME_SYNQ_SET *)(puchRecvData + sizeof(FRAME_HEADER));
	RES_TIME_SYNQ_SET *pstResTimeSynqSet = (RES_TIME_SYNQ_SET *)(puchCmdResult);
	
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

	fprintf(stderr, "AZ EL Offset Set executed\n");
	return sizeof(RES_AZ_EL_OFFSET_SET);
}

// int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
// {

// }

// int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
// {

// }

// int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
// {

// }

// int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
// {

// }

// int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
// {

// }

// int trackingSelect(unsigned char* puchRecvData, unsigned char* puchCmdResult)
// {

// }