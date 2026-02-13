#include <gtest/gtest.h>
extern "C" {
#include <event2/buffer.h>
#include <string.h>

#include "internal.h"
}

/* ============================= */
/* Fake stubs                    */
/* ============================= */
extern "C" {

void event_active(struct event*, int, short) {}
void event_add(struct event*, const struct timeval*) {}
void event_del(struct event*) {}

void ioMarkChannelDead(IO_CHANNEL*, IO_EVENT_TYPE) {}
IO_CHANNEL* ioFindChannelByWorkerId(EVENT_ENGINE*, char) { return NULL; }
int ioIsChannelAlive(IO_CHANNEL*) { return 0; }

}

/* ============================= */
/* Test Fixture                  */
/* ============================= */

class UdsConsumeTest : public ::testing::Test {
protected:
    EVENT_ENGINE engine{};
    IO_CHANNEL channel{};
    ACU_CTRL_CTX ctx{};

    void SetUp() override {
        memset(&engine, 0, sizeof(engine));
        memset(&channel, 0, sizeof(channel));
        memset(&ctx, 0, sizeof(ctx));

        channel.pstReadBuffer = evbuffer_new();
        channel.pstWriteBuffer = evbuffer_new();
        channel.pstRequestBuffer = evbuffer_new();

        channel.pstEventEngine = &engine;
        engine.pvSharedData = &ctx;
    }

    void TearDown() override {
        evbuffer_free(channel.pstReadBuffer);
        evbuffer_free(channel.pstWriteBuffer);
        evbuffer_free(channel.pstRequestBuffer);
    }
};

/* ============================= */
/* Test 1: Partial frame         */
/* ============================= */

TEST_F(UdsConsumeTest, PartialFrameShouldWait)
{
    const char garbage[] = {0x01,0x02,0x03};
    evbuffer_add(channel.pstReadBuffer, garbage, sizeof(garbage));

    channel.ePendingLogicEvent = IO_EVT_RX_DATA;

    commandEventCb(0,0,&channel);

    EXPECT_EQ(evbuffer_get_length(channel.pstReadBuffer), sizeof(garbage));
}

/* ============================= */
/* Test 2: Garbage then valid    */
/* ============================= */

TEST_F(UdsConsumeTest, GarbageThenSync)
{
    const char garbage[] = {0x11,0x22,0x33,0x44};
    evbuffer_add(channel.pstReadBuffer, garbage, sizeof(garbage));

    channel.ePendingLogicEvent = IO_EVT_RX_DATA;

    commandEventCb(0,0,&channel);

    /* 최소 1바이트는 drain 되었어야 함 */
    EXPECT_LT(evbuffer_get_length(channel.pstReadBuffer), sizeof(garbage));
}

/* ============================= */
/* Test 3: Multiple frames       */
/* ============================= */

TEST_F(UdsConsumeTest, MultipleFrames)
{
    /* 실제 frame 생성 함수가 있다면 createCmdRequest 사용 */
    char dummy[128];
    memset(dummy,0,sizeof(dummy));

    /* frame 헤더가 있다고 가정하고 2개 연속 추가 */
    evbuffer_add(channel.pstReadBuffer, dummy, 32);
    evbuffer_add(channel.pstReadBuffer, dummy, 32);

    channel.ePendingLogicEvent = IO_EVT_RX_DATA;

    commandEventCb(0,0,&channel);

    /* 처리 후 buffer는 줄어들었어야 함 */
    EXPECT_LT(evbuffer_get_length(channel.pstReadBuffer), 64);
}
