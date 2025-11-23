#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

extern "C" {
#include "../udpSvr.h"
#include "../icdCommand.h"
}

/* ======= UDP 테스트용 글로벌 상태 ======= */
static std::thread g_serverThread;
static bool g_serverRunning = false;

void startServer()
{
    g_serverRunning = true;
    run();
    g_serverRunning = false;
}

class UdpServerTest : public ::testing::Test {
protected:
    int clientSock = -1;

    void SetUp() override {

        g_serverThread = std::thread(startServer);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        clientSock = socket(AF_INET, SOCK_DGRAM, 0);
        ASSERT_NE(clientSock, -1) << "UDP 소켓 생성 실패!";

        struct sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port   = htons(UDP_CLIENT_PORT);
        inet_pton(AF_INET, CLIENT_IP, &local.sin_addr);

        ASSERT_GE(bind(clientSock, (sockaddr*)&local, sizeof(local)), 0)
            << "UDP 클라이언트 포트 바인딩 실패!";
    }

    void TearDown() override {
        close(clientSock);

        kill(getpid(), SIGINT);

        if (g_serverThread.joinable())
            g_serverThread.join();
    }
};


/* =============================== KEEP ALIVE 테스트 ============================ */

TEST_F(UdpServerTest, ReqKeepAlive_ResponseSuccess)
{
    unsigned char sendBuf[256];
    unsigned char recvBuf[256];

    struct sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port   = htons(UDP_SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    MSG_ID stMsg {};
    stMsg.uchSrcId = 1;
    stMsg.uchDstId = 1;

    int sendSize = 0;
    ASSERT_EQ(makeReqFrame(CMD_KEEP_ALIVE, &stMsg, sendBuf, &sendSize), FRAME_OK);

    ssize_t sent = sendto(clientSock, sendBuf, sendSize, 0,
                          (sockaddr*)&server, sizeof(server));

    ASSERT_EQ(sent, sendSize);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    socklen_t addrLen = sizeof(server);
    ssize_t recvLen = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0,
                               (sockaddr*)&server, &addrLen);

    ASSERT_GT(recvLen, 0) << "응답 없음";

    EXPECT_EQ(responseFrame(recvBuf, &stMsg, recvLen), FRAME_OK)
        << "KEEP-ALIVE 응답 파싱 실패";
}


/* =============================== IBIT 테스트 ============================ */

TEST_F(UdpServerTest, ReqIBit_ResponseSuccess)
{
    unsigned char sendBuf[256];
    unsigned char recvBuf[256];

    struct sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port   = htons(UDP_SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    MSG_ID stMsg {};
    stMsg.uchSrcId = 1;
    stMsg.uchDstId = 1;

    int sendSize = 0;
    ASSERT_EQ(makeReqFrame(CMD_IBIT, &stMsg, sendBuf, &sendSize), FRAME_OK);

    ssize_t sent = sendto(clientSock, sendBuf, sendSize, 0,
                          (sockaddr*)&server, sizeof(server));

    ASSERT_EQ(sent, sendSize);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    socklen_t addrLen = sizeof(server);
    ssize_t recvLen = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0,
                               (sockaddr*)&server, &addrLen);

    ASSERT_GT(recvLen, 0) << "UDP 응답 없음";

    EXPECT_EQ(responseFrame(recvBuf, &stMsg, recvLen), FRAME_OK)
        << "IBIT 응답 파싱 실패";

    RES_IBIT* pRes = (RES_IBIT*)(recvBuf + sizeof(FRAME_HEADER));
    EXPECT_EQ(pRes->chBitTotResult, 0x01);
    EXPECT_EQ(pRes->chPositionResult, 0x01);
}
