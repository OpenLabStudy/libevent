#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <event2/event.h>


extern "C" {
#include "netModule/protocols/netContext.h"
#include "netModule/protocols/udp.h"
}
   

using namespace std::chrono_literals;

class UdpTest : public ::testing::Test {
protected:
    event_base* base{};
    UDP_CTX server{};

    void SetUp() override {
        base = event_base_new();
        ASSERT_NE(base, nullptr);
        udpInit(&server, base, 0x10, NET_MODE_SERVER);
    }

    void TearDown() override {
        udpStop(&server);
        if (base) event_base_free(base);
    }
};

TEST_F(UdpTest, ServerBindAndSendFromClient) {
    constexpr unsigned short kPort = 40002;
    ASSERT_EQ(udpServerStart(&server, kPort), 0);
    std::thread loop([this] { event_base_dispatch(base); });
    std::this_thread::sleep_for(100ms);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);

    const char* msg = "udp-test";
    ssize_t n = sendto(sock, msg, strlen(msg), 0, (sockaddr*)&dst, sizeof(dst));
    EXPECT_GT(n, 0);
    close(sock);

    event_base_loopexit(base, nullptr);
    loop.join();
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
