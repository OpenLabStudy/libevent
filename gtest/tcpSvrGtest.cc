#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

extern "C" {
#include "../tcpSvr.h"
#include "../icdCommand.h"
}

/* ======= 테스트 준비 ======= */
static std::thread g_serverThread;
static bool g_serverRunning = false;

void startServer()
{
    g_serverRunning = true;
    run();
    g_serverRunning = false;
}

class TcpServerTest : public ::testing::Test {
protected:
    int clientSock = -1;

    void SetUp() override {
        g_serverThread = std::thread(startServer);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        clientSock = socket(AF_INET, SOCK_STREAM, 0);

        struct sockaddr_in serverAddr {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(5000);
        inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

        ASSERT_GE(connect(clientSock, (sockaddr*)&serverAddr, sizeof(serverAddr)), 0)
            << "서버 연결 실패!";
    }

    void TearDown() override {
        close(clientSock);
        kill(getpid(), SIGINT);
        if (g_serverThread.joinable()) g_serverThread.join();
    }
};


/* ======= 실제 테스트 ======= */

TEST_F(TcpServerTest, ReqKeepAlive_ResponseSuccess)
{
    unsigned char sendBuf[256];
    unsigned char recvBuf[256];

    MSG_ID stMsgId {};
    stMsgId.uchSrcId = 1;
    stMsgId.uchDstId = 1;

    int frameSize = 0;
    
    /* 요청 프레임 생성 */
    ASSERT_EQ(makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, sendBuf, &frameSize), FRAME_OK);

    /* 서버로 전송 */
    ssize_t sent = send(clientSock, sendBuf, frameSize, 0);
    ASSERT_EQ(sent, frameSize);

    /* 응답 읽기 */
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ssize_t recvLen = recv(clientSock, recvBuf, sizeof(recvBuf), 0);

    ASSERT_GT(recvLen, 0) << "서버 응답 없음";

    /* 프레임 파싱 */
    unsigned short cmdOut = 0;
    ASSERT_EQ(requestFrame(sendBuf, &stMsgId, frameSize, &cmdOut), FRAME_OK);

    /* 응답 검증 */
    EXPECT_EQ(responseFrame(recvBuf, &stMsgId, recvLen), FRAME_OK)
        << "Response frame invalid";
}

TEST_F(TcpServerTest, ReqIBit_ResponseSuccess)
{
    unsigned char sendBuf[256];
    unsigned char recvBuf[256];

    MSG_ID stMsgId {};
    stMsgId.uchSrcId = 1;
    stMsgId.uchDstId = 1;

    int frameSize = 0;

    /* 요청 프레임 생성 */
    ASSERT_EQ(makeReqFrame(CMD_IBIT, &stMsgId, sendBuf, &frameSize), FRAME_OK);

    /* 서버로 전송 */
    ssize_t sent = send(clientSock, sendBuf, frameSize, 0);
    ASSERT_EQ(sent, frameSize) << "요청 프레임 전송 오류";

    /* 응답 수신 */
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ssize_t recvLen = recv(clientSock, recvBuf, sizeof(recvBuf), 0);

    ASSERT_GT(recvLen, 0) << "서버로부터 응답이 오지 않음";

    /* 응답 프레임 파싱 */
    EXPECT_EQ(responseFrame(recvBuf, &stMsgId, recvLen), FRAME_OK)
        << "IBIT 응답 프레임 파싱 실패";

    /* === 실제 페이로드 검증 === */
    RES_IBIT* pRes = (RES_IBIT*)(recvBuf + sizeof(FRAME_HEADER));

    EXPECT_EQ(pRes->chBitTotResult, 0x01) << "IBIT total result invalid";
    EXPECT_EQ(pRes->chPositionResult, 0x01) << "IBIT position result invalid";
}

