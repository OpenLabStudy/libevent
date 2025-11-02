/**
 * @file tcpGtest.cpp
 * @brief TCP 서버/클라이언트 GoogleTest
 */

 #include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <event2/event.h>

 extern "C" {
 #include "netModule/core/frame.h"
 #include "netModule/core/icdCommand.h"
 #include "netModule/core/netUtil.h"
 #include "netModule/protocols/tcp.h"
 #include "netModule/protocols/commonSession.h"
 }


using namespace std::chrono_literals;

class TcpTest : public ::testing::Test {
protected:
    event_base* pstSvrEventBase{};
    TCP_SERVER_CTX stTcpSvr{};
    event_base* pstClnEventBase{};
    TCP_CLIENT_CTX stTcpCln{};

    void SetUp() override {
        pstSvrEventBase = event_base_new();
        pstClnEventBase = event_base_new();
        ASSERT_NE(pstSvrEventBase, nullptr);
        tcpSvrInit(&stTcpSvr, pstSvrEventBase, 0x01, TCP_SERVER);
        tcpClnInit(&stTcpCln, pstClnEventBase, 0x02, TCP_CLIENT);
    }

    void TearDown() override {
        tcpSvrStop(&stTcpSvr);
        tcpClnStop(&stTcpCln);
        if (pstSvrEventBase) 
            event_base_free(pstSvrEventBase);
    }
};
#if 0
TEST_F(TcpTest, ServerClient_Connection) {
    constexpr unsigned short kPort = 40000;
    ASSERT_EQ(tcpServerStart(&stTcpSvr, kPort), 0);

    // ======= 루프 타임아웃 이벤트 추가 (3초 후 종료) =======
    struct timeval tv = {3, 0}; // 3초 후 루프 종료
    event_base_once(pstSvrEventBase, -1, EV_TIMEOUT, [](evutil_socket_t, short, void* arg) {
        event_base_loopexit(static_cast<event_base*>(arg), nullptr);
    }, pstSvrEventBase, &tv);
    // ======================================================

    std::thread loop([this] { event_base_dispatch(pstSvrEventBase); });
    std::this_thread::sleep_for(100ms);

    ASSERT_EQ(tcpClientConnect(&stTcpCln, "127.0.0.1", kPort), 0);

    std::this_thread::sleep_for(300ms);

    EXPECT_GE(stTcpSvr.stNetBase.stCoreCtx.iClientCount, 1);
    loop.join();
}
#endif

TEST_F(TcpTest, FrameSend_KeepAlive) {
    constexpr unsigned short kPort = 40001;
    tcpServerStart(&stTcpSvr, kPort);

    struct timeval tv = {3, 0};
    event_base_once(pstSvrEventBase, -1, EV_TIMEOUT, [](evutil_socket_t, short, void* arg) {
        event_base_loopexit(static_cast<event_base*>(arg), nullptr);
    }, pstSvrEventBase, &tv);

    std::thread loop([this] { event_base_dispatch(pstSvrEventBase); });
    std::this_thread::sleep_for(100ms);

    tcpClientConnect(&stTcpCln, "127.0.0.1", kPort);
    std::this_thread::sleep_for(300ms);

    MSG_ID id{ stTcpCln.stNetBase.uchMyId, stTcpSvr.stNetBase.uchMyId };
    const char* msg = "hello";
    EXPECT_EQ(writeFrame(stTcpCln.pstBufferEvent, CMD_KEEP_ALIVE, &id, 0, msg, strlen(msg)), 1);

    loop.join();
}


int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
