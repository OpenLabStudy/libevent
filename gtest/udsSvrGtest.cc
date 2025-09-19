// test_uds_server_run.cc
#include <gtest/gtest.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <vector>

extern "C" {
#include "../sockSession.h"
#include "../frame.h"
#include "../icdCommand.h"
// #include "uds.h"

/* udsSvr.c에 있는 심볼들 (테스트에서 직접 호출) */
int run(void);
void udsSvrStop(void);
int  udsSvrIsRunning(void);
}

/* --- 작은 유틸 --- */
static bool waitForFileExists(const char* path, int timeout_ms=1500) {
    const int step=10;
    int waited=0; 
    struct stat st{};
    while (waited < timeout_ms) {
        if (stat(path, &st) == 0) return true;
        usleep(step*1000); waited += step;
    }
    return false;
}

static int connectUds(const char* udsPath) {
    int iFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (iFd < 0) 
        return -1;
    struct sockaddr_un un{};
    un.sun_family = AF_UNIX;
    size_t len = strlen(udsPath);
    memcpy(un.sun_path, udsPath, len+1);
    if (::connect(iFd, (struct sockaddr*)&un,
                  (socklen_t)(offsetof(struct sockaddr_un, sun_path)+len+1)) < 0) {
        ::close(iFd); 
        return -1;
    }
    return iFd;
}
#if 1
static uint8_t crc8_xor(const uint8_t* p, size_t n) {
    uint8_t c=0; 
    for (size_t i=0;i<n;i++) 
        c^=p[i]; 
    return c;
}

static bool readOneFrame(int fd, unsigned short expected_cmd,
                         char* payload_buf, int iPacketSize, int* iPayloadLen,
                         int timeout_ms=1500)
{
    FRAME_HEADER hdr{};
    size_t got=0;
    const uint8_t* p = (const uint8_t*)&hdr;
    int waited=0;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    while (got < sizeof(hdr) && waited < timeout_ms) {
        ssize_t r = ::recv(fd, (void*)(p+got), sizeof(hdr)-got, 0);
        fprintf(stderr,"recv size is %d\n",r);
        if (r > 0) {
            got += (size_t)r; 
            continue; 
        }
        if (r == 0) 
            return false;
        if (errno==EAGAIN || errno==EWOULDBLOCK) { 
            usleep(10*1000); 
            waited+=10; 
            continue; 
        }
        if (errno==EINTR) 
            continue;
        return false;
    }

    if (got < sizeof(hdr))
        return false;

    if (ntohs(hdr.unStx) != STX_CONST)
        return false;
        
    const int32_t data_len = ntohl(hdr.iDataLength);

    if (data_len < 0)
        return false;

    if (ntohs(hdr.unCmd) != expected_cmd)
        return false;

    std::vector<uint8_t> payload;
    payload.resize((size_t)data_len);
    got=0; waited=0;
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    while (got < (size_t)data_len && waited < timeout_ms) {
        ssize_t r = ::recv(fd, payload.data()+got, (size_t)data_len-got, 0);
        if (r > 0) { 
            got += (size_t)r; 
            continue; 
        }

        if (r == 0) 
            return false;

        if (errno==EAGAIN || errno==EWOULDBLOCK) { 
            usleep(10*1000); 
            waited+=10; 
            continue; 
        }

        if (errno==EINTR)
            continue;

        return false;
    }
    if (got < (size_t)data_len)
        return false;
fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    FRAME_TAIL tail{};
    got=0; 
    waited=0; 
    p=(const uint8_t*)&tail;
    while (got < sizeof(tail) && waited < timeout_ms) {
        ssize_t r = ::recv(fd, (void*)(p+got), sizeof(tail)-got, 0);
        if (r > 0) { 
            got += (size_t)r; 
            continue; 
        }

        if (r == 0) 
            return false;

        if (errno==EAGAIN || errno==EWOULDBLOCK) {
            usleep(10*1000);
            waited+=10;
            continue;
        }

        if (errno==EINTR)
            continue;

        return false;
    }

    if (got < sizeof(tail))
        return false;

    if (ntohs(tail.unEtx) != ETX_CONST)
        return false;

    if (crc8_xor(payload.data(), payload.size()) != (uint8_t)tail.uchCrc)
        return false;

    if (iPayloadLen) 
        *iPayloadLen = payload.size();
    if (payload_buf && iPacketSize >= payload.size()) {
        memcpy(payload_buf, payload.data(), payload.size());
    }
    return true;
}
#endif
/* --- 서버 스레드 --- */
static void* serverThread(void*) {
    (void)run();                   // ← udsSvr.c의 run()을 “그대로” 실행
    return nullptr;
}

/* --- 픽스처 --- */
class UdsSvrRunTest : public ::testing::Test {
protected:
    pthread_t tid_{};

    void SetUp() override {
        unlink(UDS1_PATH);
        // 서버 기동
        ASSERT_EQ(0, pthread_create(&tid_, nullptr, serverThread, nullptr));
        // 소켓 파일 생성 대기
        ASSERT_TRUE(waitForFileExists(UDS1_PATH, 2000)) << "UDS not ready";
        // (선택) 러닝 여부 점검
        EXPECT_TRUE(udsSvrIsRunning());
    }

    void TearDown() override {
        // 테스트 종료 → 서버 루프 중단
        udsSvrStop();
        pthread_join(tid_, nullptr);
        unlink(UDS1_PATH);
    }
};

/* 1) 접속/해지 */
TEST_F(UdsSvrRunTest, ConnectDisconnect) {
    int fd1 = connectUds(UDS1_PATH);
    ASSERT_GE(fd1, 0) << strerror(errno);
    ::close(fd1);

    // 재접속 가능해야 함
    int fd2 = connectUds(UDS1_PATH);
    ASSERT_GE(fd2, 0) << "reconnect failed";
    ::close(fd2);
}
#if 1
/* 2) KEEP_ALIVE 라운드트립 */
TEST_F(UdsSvrRunTest, KeepAliveCommandTest) {
    int iFd = connectUds(UDS1_PATH);
    ASSERT_GE(iFd, 0) << strerror(errno);

    // 기존 코드를 최대한 재사용하기 위해 bufferevent를 래핑해 requestFrame() 호출
    struct event_base* pstEventBase = event_base_new();
    ASSERT_NE(pstEventBase, nullptr);
    struct bufferevent* pstBufferEvent = bufferevent_socket_new(pstEventBase, iFd, BEV_OPT_CLOSE_ON_FREE);
    ASSERT_NE(pstBufferEvent, nullptr);
    bufferevent_enable(pstBufferEvent, EV_READ|EV_WRITE);
    bufferevent_setwatermark(pstBufferEvent, EV_READ, sizeof(FRAME_HEADER), READ_HIGH_WM);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    MSG_ID stMsgId{};
    stMsgId.uchSrcId = 0x11;
    stMsgId.uchDstId = 0x22;
    sleep(1);
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    ASSERT_EQ(1, requestFrame(pstBufferEvent, &stMsgId, CMD_KEEP_ALIVE));
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    bufferevent_flush(pstBufferEvent, EV_WRITE, BEV_FLUSH);
fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    char* pchResPayload;
    int iResFrameSize = sizeof(FRAME_HEADER) + sizeof(RES_KEEP_ALIVE) + sizeof(FRAME_TAIL);
    int iResOutSize=1;
    pchResPayload = (char*)malloc(iResFrameSize);
fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    ASSERT_TRUE(readOneFrame(iFd, CMD_KEEP_ALIVE, pchResPayload, iResFrameSize, &iResOutSize, 1500));
    fprintf(stderr,"### %s():%d ###\n",__func__,__LINE__);
    ASSERT_EQ(iResOutSize, sizeof(RES_KEEP_ALIVE));
    RES_KEEP_ALIVE* res = (RES_KEEP_ALIVE*)pchResPayload;
    EXPECT_EQ(res->chResult, 0x01);

    free(pchResPayload);

    bufferevent_free(pstBufferEvent);  // cfd도 함께 close
    event_base_free(pstEventBase);
}
#endif
/* 3) quit 동등 시나리오(클라이언트 종료) */
TEST_F(UdsSvrRunTest, QuitLikeClose) {
    int iFd = connectUds(UDS1_PATH);
    ASSERT_GE(iFd, 0);
    ::close(iFd);  // quit과 동일한 효과

    // 서버는 계속 동작해야 함 → 재접속 확인
    int iFd2 = connectUds(UDS1_PATH);
    ASSERT_GE(iFd2, 0);
    ::close(iFd2);
}
