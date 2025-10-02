#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "../mutexQueue.h"
}

using namespace std::chrono_literals;

class MutexQueueTest : public ::testing::Test {
protected:
    MUTEX_QUEUE* pMutexQueue = nullptr;
    const size_t QSIZE = 4;

    void SetUp() override {
        pMutexQueue = newMutexQueue(QSIZE);
        ASSERT_NE(pMutexQueue, nullptr);
    }

    void TearDown() override {
        // 큐 안에 남아 있을 수 있는 포인터를 비워서 누수 방지
        void* pvData = nullptr;
        while ((pvData = popMutexQueueWaitTimeout(pMutexQueue, 0)) != nullptr) {
            // 테스트에서 동적할당한 경우 여기서 free/delete 가능
            // 이 샘플은 테스트 본문에서 해제 처리
        }
        freeMutexQueue(pMutexQueue);
        pMutexQueue = nullptr;
    }
};

// 1) Non-blocking push: 빈 큐에서는 성공, 가득 차면 실패(0x00)
TEST_F(MutexQueueTest, PushNoWait_FillThenFail) {
    int iData1=1,iData2=2,iData3=3,iData4=4,iData5=5;

    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData1), 0x01);
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData2), 0x01);
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData3), 0x01);
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData4), 0x01);

    // 가득 찬 상태에서 한 번 더 시도 → 실패(0x00)
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData5), 0x00);

    // 모두 꺼내서 확인
    EXPECT_EQ(popMutexQueueWaitTimeout(pMutexQueue, 0), &iData1);
    EXPECT_EQ(popMutexQueueWaitTimeout(pMutexQueue, 0), &iData2);
    EXPECT_EQ(popMutexQueueWaitTimeout(pMutexQueue, 0), &iData3);
    EXPECT_EQ(popMutexQueueWaitTimeout(pMutexQueue, 0), &iData4);
    EXPECT_EQ(popMutexQueueWaitTimeout(pMutexQueue, 0), nullptr);
}

// 2) Pop(0ms): 비어있으면 즉시 NULL
TEST_F(MutexQueueTest, PopImmediate_WhenEmptyReturnsNull) {
    void* pvData = popMutexQueueWaitTimeout(pMutexQueue, 0);
    EXPECT_EQ(pvData, nullptr);
}

// 3) Pop(-1): 생산자 푸시까지 대기 후 정확히 수신
TEST_F(MutexQueueTest, PopWaitsUntilProducer) {
    int* piData = new int(42);

    std::atomic<bool> consumer_started{false};
    std::atomic<void*> received{nullptr};

    std::thread consumer([&](){
        consumer_started.store(true, std::memory_order_release);
        void* pvData = popMutexQueueWaitTimeout(pMutexQueue, -1); // 무한 대기
        received.store(pvData, std::memory_order_release);
    });

    // 소비자 스레드가 확실히 시작됐는지 확인
    while (!consumer_started.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(5ms);
    }

    // 생산자가 약간 지연 후 push
    std::this_thread::sleep_for(50ms);
    pushMutexQueueWait(pMutexQueue, piData);

    consumer.join();
    EXPECT_EQ(received.load(std::memory_order_acquire), piData);

    delete piData;
}

// 4) PushWait: 큐가 가득 찼을 때 소비자가 하나 꺼낼 때까지 블록됨
TEST_F(MutexQueueTest, PushWait_BlocksUntilSpaceAvailable) {
    int iData1=1,iData2=2,iData3=3,iData4=4,iData5=5;

    // 큐를 가득 채운다
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData1), 0x01);
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData2), 0x01);
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData3), 0x01);
    EXPECT_EQ(pushMutexQueueNoWait(pMutexQueue, &iData4), 0x01);

    std::atomic<bool> producer_entered{false};
    std::atomic<bool> producer_done{false};

    // 소비자가 약간 뒤에 하나 꺼내서 공간을 만들어줌
    std::thread consumer([&](){
        std::this_thread::sleep_for(30ms);
        void* p = popMutexQueueWaitTimeout(pMutexQueue, -1);
        (void)p;
    });

    // 가득 찬 상태에서 pushMutexQueueWait는 블록되어야 함
    auto t0 = std::chrono::steady_clock::now();
    std::thread producer([&](){
        producer_entered.store(true, std::memory_order_release);
        pushMutexQueueWait(pMutexQueue, &iData5);  // 소비자가 꺼낼 때까지 대기
        producer_done.store(true, std::memory_order_release);
    });

    // 프로듀서가 진입했는지 확인
    while (!producer_entered.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(1ms);
    }

    producer.join();
    auto t1 = std::chrono::steady_clock::now();
    consumer.join();

    // 실제로 어느 정도 대기했는지(>= 소비자 sleep 시간 근처) 확인 (너무 타이트하지 않게)
    auto waited_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_TRUE(producer_done.load(std::memory_order_acquire));
    EXPECT_GE(waited_ms, 20); // 30ms보다 조금 낮춰 여유있게 체크

    // 이제 iData5를 포함해 총 4개가 있어야 함 (iData1..iData4 중 하나는 소비자가 꺼냈고 iData5가 들어왔으므로)
    // 남은 것들을 꺼내보며 iData5가 들어왔는지 확인
    std::vector<void*> got;
    for (;;) {
        void* p = popMutexQueueWaitTimeout(pMutexQueue, 0);
        if (!p) break;
        got.push_back(p);
    }
    bool has_iData5 = false;
    for (auto p: got) {
        if (p == &iData5) { 
            has_iData5 = true; 
            break; 
        }
    }
    EXPECT_TRUE(has_iData5);
}

// 5) FIFO 순서 보장
TEST_F(MutexQueueTest, FifoOrder) {
    const int N = 100;
    std::vector<int*> data;
    data.reserve(N);
    for (int i=0;i<N;++i) 
        data.push_back(new int(i));

    // 생산자 스레드: 빠르게 밀어넣기(용량 초과 시 pushWait로 대기)
    std::thread producer([&](){
        for (int i=0;i<N;++i) {
            pushMutexQueueWait(pMutexQueue, data[i]);
        }
    });

    // 소비자 스레드: 순서대로 pop(-1)
    std::vector<int> out;
    out.reserve(N);
    std::thread consumer([&](){
        for (int i=0;i<N;++i) {
            void* p = popMutexQueueWaitTimeout(pMutexQueue, -1);
            ASSERT_NE(p, nullptr);
            out.push_back(*static_cast<int*>(p));
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ((int)out.size(), N);
    for (int i=0;i<N;++i) {
        EXPECT_EQ(out[i], i);
    }

    for (auto p: data) 
        delete p;
}
