#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include "../udsSvr.h"
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

class UdsServerTest : public ::testing::Test {
protected:
    int clientSock = -1;

    void SetUp() override {

        unlink("/tmp/uds1.sock");

        g_serverThread = std::thread(startServer);

        // 서버 준비 시간
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 클라이언트 UDS 소켓 생성
        clientSock = socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_NE(clientSock, -1) << "UDS 소켓 생성 실패!";

        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, "/tmp/uds1.sock");

        ASSERT_GE(connect(clientSock, (struct sockaddr*)&addr, sizeof(addr)), 0)
            << "UDS 서버 연결 실패!";
    }

    void TearDown() override {
        close(clientSock);

        // SIGINT 전달하여 서버 종료
        kill(getpid(), SIGINT);

        if (g_serverThread.joinable())
            g_serverThread.join();

        unlink("/tmp/uds1.sock");
    }
};


/* ======= KEEP ALIVE 테스트 ======= */

TEST_F(UdsServerTest, ReqKeepAlive_ResponseSuccess)
{
    unsigned char sendBuf[256];
    unsigned char recvBuf[256];

    MSG_ID stMsgId {};
    stMsgId.uchSrcId = 1;
    stMsgId.uchDstId = 1;

    int frameSize = 0;

    ASSERT_EQ(makeReqFrame(CMD_KEEP_ALIVE, &stMsgId, sendBuf, &frameSize), FRAME_OK);

    ssize_t sent = send(clientSock, sendBuf, frameSize, 0);
    ASSERT_EQ(sent, frameSize);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ssize_t recvLen = recv(clientSock, recvBuf, sizeof(recvBuf), 0);
    ASSERT_GT(recvLen, 0) << "서버 응답 없음";

    EXPECT_EQ(responseFrame(recvBuf, &stMsgId, recvLen), FRAME_OK)
        << "KEEP-ALIVE 응답 프레임 검증 실패";
}


/* ======= IBIT 테스트 ======= */

TEST_F(UdsServerTest, ReqIBit_ResponseSuccess)
{
    unsigned char sendBuf[256];
    unsigned char recvBuf[256];

    MSG_ID stMsgId {};
    stMsgId.uchSrcId = 1;
    stMsgId.uchDstId = 1;

    int frameSize = 0;

    ASSERT_EQ(makeReqFrame(CMD_IBIT, &stMsgId, sendBuf, &frameSize), FRAME_OK);

    ssize_t sent = send(clientSock, sendBuf, frameSize, 0);
    ASSERT_EQ(sent, frameSize);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ssize_t recvLen = recv(clientSock, recvBuf, sizeof(recvBuf), 0);
    ASSERT_GT(recvLen, 0) << "UDS 기반 서버로부터 응답 없음";

    EXPECT_EQ(responseFrame(recvBuf, &stMsgId, recvLen), FRAME_OK)
        << "IBIT 응답 프레임 파싱 실패";

    RES_IBIT* pRes = (RES_IBIT*)(recvBuf + sizeof(FRAME_HEADER));
    EXPECT_EQ(pRes->chBitTotResult, 0x01);
    EXPECT_EQ(pRes->chPositionResult, 0x01);
}
