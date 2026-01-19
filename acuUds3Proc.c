#include "ipcUtil.h"
#include "acuCtrl.h"

static void recvAzElFromSensorFusion(int iFd, short nEvent, void* pvData)
{
    IO_CHANNEL* pstIoChannel = (IO_CHANNEL *)pvData;
    IO_EVENT_TYPE eEventType = pstIoChannel->ePendingLogicEvent;

    unsigned char auchRecvBuffer[UDS_MAX_BUFFER_SIZE];
    unsigned short unCmd = 0;
    FRAME_ERR eErr;

    fprintf(stderr,"### %s():%d ###\n", __func__, __LINE__);
    switch (eEventType) {
    case IO_EVT_RX_DATA:
        while (1) {
            size_t tRecvLen = evbuffer_get_length(pstIoChannel->pstReadBuffer);
            /* 최소 헤더도 없으면 중단 */
            if (tRecvLen < sizeof(FRAME_HEADER))
                break;

            memset(auchRecvBuffer, 0x00, sizeof(auchRecvBuffer));
            int iCopyLen = evbuffer_copyout(pstIoChannel->pstReadBuffer,
                                            auchRecvBuffer, tRecvLen);
            /* frameDecode에 대한 처리가 완전한지 확인 필요*/
            eErr = frameDecode(auchRecvBuffer, iCopyLen, FRAME_TYPE_RESPONSE, &unCmd);
            if (eErr != FRAME_OK) {
                fprintf(stderr, "[UDS-SVR] frameDecode ERR: %s\n", frameErrToStr(eErr));
                int iOffset = findFrameHeader(auchRecvBuffer, iCopyLen);
                if (iOffset >= 0) {
                    /* 앞부분 garbage 제거 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iOffset);
                    fprintf(stderr,"[UDS-SVR] resync: drop %d bytes, retry decode\n", iOffset);
                } else if (iOffset == -2) {
                    /* STX half-match: 데이터 더 수신 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen-1);
                    fprintf(stderr,"[UDS-SVR] STX half match, wait more data\n");
                } else {
                    /* STX 자체가 없음 → 전부 드랍 */
                    evbuffer_drain(pstIoChannel->pstReadBuffer, iCopyLen);
                    fprintf(stderr, "[UDS-SVR] no STX, drop all\n");
                }
                continue;
            }
            int iFrameSize = getFrameSizeWithCmd(unCmd, FRAME_TYPE_RESPONSE);
            fprintf(stderr,"### %s():%d %d ###\n", __func__, __LINE__, iFrameSize);
            /* === 프레임 소비 === */
            evbuffer_drain(pstIoChannel->pstReadBuffer, iFrameSize + sizeof(unsigned int));
            RES_AZ_EL_DATA* pstAzElData;
            pstAzElData = (RES_AZ_EL_DATA *)(auchRecvBuffer + sizeof(FRAME_HEADER));
            fprintf(stderr,"Azimuth: %lf, Elevation: %lf\n", pstAzElData->dAz, pstAzElData->dEl);

            //TODO ACU UART로 명령 전송 및 응답 수신
        }
        break;

    case IO_EVT_CHANNEL_CLOSED:
    case IO_EVT_ERROR:
        printf("[UDS-SVR] channel closed fd=%d\n", pstIoChannel->iFd);
        event_active(pstIoChannel->pstShutdownEvent, 0, 0);
        break;

    default:
        break;
    }

    pstIoChannel->ePendingLogicEvent = IO_EVENT_NONE;
}

/* ============================================================
* Accept 콜백
* ============================================================ */
static void acceptUds3Cb(evutil_socket_t iListenFd, short nKindOfEvent, void* pvArg)
{
    (void)nKindOfEvent;
    EVENT_ENGINE* pstEventEngine = (EVENT_ENGINE *)pvArg;

    struct sockaddr_in stClientAddr;
    socklen_t uiClientLen = sizeof(stClientAddr);

    int iClientSock = accept(iListenFd, (struct sockaddr*)&stClientAddr, &uiClientLen);
    if (iClientSock < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[UDS_3_SVR] accept");
        return;
    }

    printf("[UDS_3_SVR] New client FD=%d\n", iClientSock);
    netSetNonblock(iClientSock);

    eventSourceCreateWithBev(pstEventEngine, iClientSock,
        TYPE_TCP_SVR, ROLE_REQUESTER,
        NULL, NULL, recvAzElFromSensorFusion);
}

void createUds3EventEngine(EVENT_ENGINE *pstEventEngine)
{
    /* Accept 이벤트 등록 */
    int iListenUds3Fd = netUdsCreateServer(UDS_3_PATH);
    if (iListenUds3Fd < 0) {
        fprintf(stderr, "[ACU_CTRL] netUdsCreateServer() failed\n");
        return EXIT_FAILURE;
    }

    struct event*   pstEventAcceptUds3 = event_new(pstEventEngine->pstEventBase, iListenUds3Fd,
            EV_READ | EV_PERSIST, acceptUds3Cb, pstEventEngine);
    event_add(pstEventAcceptUds3, NULL);

}