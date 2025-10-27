// test_uds_server_run.cc
#include <gtest/gtest.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <vector>
#include <chrono>

// TCP에 필요한 헤더
#include <netinet/in.h>
#include <arpa/inet.h>

extern "C" {
#include "../sockSession.h"
#include "../frame.h"
#include "../icdCommand.h"

/* udsSvr.c에 있는 심볼들 (테스트에서 직접 호출) */
int run(void);
void tcpSvrStop(void);
int  tcpSvrIsRunning(void);
}

/* --- 작은 유틸 --- */
static bool waitForPortOpen(const char* host, int port, int timeout_ms = 3000) {
    const int step = 50;
    int waited = 0;
    while (waited < timeout_ms) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_in sin; memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_port   = htons(port);
            if (inet_pton(AF_INET, host, &sin.sin_addr) == 1) {
                if (::connect(fd, (struct sockaddr*)&sin, sizeof(sin)) == 0) {
                    ::close(fd);
                    return true; // 연결 성공 = 포트 열림
                }
            }
            ::close(fd);
        }
        usleep(step * 1000);
        waited += step;
    }
    return false;
}

/* --- TCP connect --- */
static int connectTcp(const char* host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sin; memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &sin.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }
    if (::connect(fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

/* === 프레임 하나 파싱 & 응답 처리 ===
 * return: 1 consumed, 0 need more, -1 fatal
 */
int checkRecvData(char* pchData, int iDataSize, void* pvResponse) {
    FRAME_HEADER *pstFrameHeader = (FRAME_HEADER *)pchData;
    unsigned short  unStx   = ntohs(pstFrameHeader->unStx);
    int iDataLength = ntohl(pstFrameHeader->iDataLength);
    unsigned short  unCmd  = ntohs(pstFrameHeader->unCmd);

    if (unStx != STX_CONST || iDataLength < 0)
        return -1;////FRAME_ERR_STX_NOT_MATCH


    unsigned char *uchPayload = NULL;
    if (iDataLength > 0) {
        uchPayload = (unsigned char *)malloc((size_t)iDataLength);
        if (!uchPayload){
            return -1;//FRAME_ERR_MEMORY_ALLOC_FAIL
        }
    }

    FRAME_TAIL *pstFrameTail = (FRAME_TAIL *)(pchData + sizeof(FRAME_HEADER) + iDataLength);
    if (ntohs(pstFrameTail->unEtx) != ETX_CONST) {
        free(uchPayload); 
        return -1;//FRAME_ERR_ETX_NOT_MATCH
    }
    /* === 응답 처리 ===
       - 기본 가정: 요청 CMD와 응답 CMD가 동일
    */
    switch (unCmd) {
        case CMD_REQ_ID: {
            RES_ID* pstResId = (RES_ID *)(pchData + sizeof(FRAME_HEADER));
            RES_ID* pstRetValue = (RES_ID *)pvResponse;
            pstRetValue->chResult = pstResId->chResult;
            break;
        }
        case CMD_KEEP_ALIVE: {
            RES_KEEP_ALIVE* pstResKeepAlive = (RES_KEEP_ALIVE *)(pchData + sizeof(FRAME_HEADER));
            RES_KEEP_ALIVE* pstRetValue = (RES_KEEP_ALIVE *)pvResponse;
            pstRetValue->chResult = pstResKeepAlive->chResult;
            break;
        }
        case CMD_IBIT: {
            RES_IBIT* pstResIbit = (RES_IBIT *)(pchData + sizeof(FRAME_HEADER));
            RES_IBIT* pstRetValue = (RES_IBIT *)pvResponse;
            pstRetValue->chPositionResult = pstResIbit->chPositionResult;
            pstRetValue->chBitTotResult = pstResIbit->chBitTotResult;
            break;
        }
        default:
            fprintf(stderr, "RES cmd=%d len=%d\n", unCmd, iDataLength);
            break;
    }
    free(uchPayload);
    return 1;//FRAME_SUCCESS;
}

/* --- 서버 스레드 --- */
static void* serverThread(void*) {
    (void)run();                   // ← udsSvr.c의 run()을 “그대로” 실행
    return nullptr;
}

static void  pump(struct event_base* pstEventBase, int ms){
    int waited=0;
    while (waited < ms) {
        event_base_loop(pstEventBase, EVLOOP_ONCE | EVLOOP_NONBLOCK);
        usleep(10*1000);
        waited += 10;
    }
};

/* --- 픽스처 --- */
class TcpSvrRunTest : public ::testing::Test {
protected:
    pthread_t tid_{};

    void SetUp() override {
        ASSERT_EQ(0, pthread_create(&tid_, nullptr, serverThread, nullptr));
        ASSERT_TRUE(waitForPortOpen("127.0.0.1", TCP_PORT, 2000)) << "TCP not ready";
        EXPECT_TRUE(tcpSvrIsRunning());
    }

    void TearDown() override {
        // 테스트 종료 → 서버 루프 중단
        tcpSvrStop();
        pthread_join(tid_, nullptr);
        unlink(UDS1_PATH);
    }
};

/* 1) 접속/해지 */
TEST_F(TcpSvrRunTest, ConnectDisconnect) {
    int fd1 = connectTcp("127.0.0.1", TCP_PORT);
    ASSERT_GE(fd1, 0) << strerror(errno);
    ::close(fd1);

    // 재접속 가능해야 함
    int fd2 = connectTcp("127.0.0.1", TCP_PORT);
    ASSERT_GE(fd2, 0) << "reconnect failed";
    ::close(fd2);
}

/* 2) KEEP_ALIVE 라운드트립 */
TEST_F(TcpSvrRunTest, KeepAliveCommandTest) {
    /* TCP 주소 준비 */
    struct sockaddr_in stSocketIn;
    memset(&stSocketIn,0,sizeof(stSocketIn));
    stSocketIn.sin_family = AF_INET;
    stSocketIn.sin_port   = htons((uint16_t)TCP_PORT);
    ASSERT_EQ(1, inet_pton(AF_INET, "127.0.0.1", &stSocketIn.sin_addr));

    // 기존 코드를 최대한 재사용하기 위해 bufferevent를 래핑해 requestFrame() 호출
    struct event_base* pstEventBase = event_base_new();
    ASSERT_NE(pstEventBase, nullptr);
    struct bufferevent* pstBufferEvent = bufferevent_socket_new(pstEventBase, -1, BEV_OPT_CLOSE_ON_FREE);
    ASSERT_NE(pstBufferEvent, nullptr);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);
    if (bufferevent_socket_connect(pstBufferEvent, (struct sockaddr*)&stSocketIn, sizeof(stSocketIn)) < 0) {
        fprintf(stderr, "Connect failed: %s\n", strerror(errno));
        bufferevent_free(pstBufferEvent);
        event_base_free(pstEventBase);
        return;
    }
    MSG_ID stMsgId{};
    stMsgId.uchSrcId = UDS1_CLIENT1_ID;
    stMsgId.uchDstId = UDS1_SERVER_ID;
    char* pchResPayload;
    int iResFrameSize = sizeof(FRAME_HEADER) + sizeof(RES_KEEP_ALIVE) + sizeof(FRAME_TAIL);
    int iResOutSize=1;
    pchResPayload = (char*)malloc(iResFrameSize);    

    // 연결 안정화까지 잠깐 펌프
    pump(pstEventBase, 200);
    ASSERT_EQ(1, requestFrame(pstBufferEvent, &stMsgId, CMD_KEEP_ALIVE));
    pump(pstEventBase, 500);
    struct evbuffer *pstEvBuffer = bufferevent_get_input(pstBufferEvent);
    int iLength = evbuffer_get_length(pstEvBuffer);
    ASSERT_EQ(iLength, iResFrameSize);
    if (evbuffer_copyout(pstEvBuffer, pchResPayload, iResFrameSize) != iLength){
        return;//EV_COPYOUT_SIZE_MISMATCH
    }
    RES_KEEP_ALIVE stResKeepAlive;
    ASSERT_EQ(1, checkRecvData(pchResPayload, iResFrameSize, (void*)&stResKeepAlive));
    ASSERT_EQ(1, stResKeepAlive.chResult);

    free(pchResPayload);
    bufferevent_free(pstBufferEvent);  // cfd도 함께 close
    event_base_free(pstEventBase);
}

/* 3) quit 동등 시나리오(클라이언트 종료) */
TEST_F(TcpSvrRunTest, QuitLikeClose) {
    int iFd = connectTcp("127.0.0.1", TCP_PORT);
    ASSERT_GE(iFd, 0);
    ::close(iFd);  // quit과 동일한 효과

}
