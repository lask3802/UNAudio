#include "Core/Queues.h"

#include <gtest/gtest.h>

#include <thread>

using una::SpscQueue;
using una::MpscQueue;

TEST(Queues, SpscFifoAcrossThreads) {
    SpscQueue<uint64_t> q(1024);
    constexpr uint64_t kCount = 2000000;
    std::thread producer([&] {
        for (uint64_t i = 0; i < kCount; ++i)
            while (!q.TryPush(i)) std::this_thread::yield();
    });
    uint64_t expected = 0, v = 0;
    while (expected < kCount) {
        if (q.TryPop(v)) {
            ASSERT_EQ(v, expected);
            ++expected;
        }
    }
    producer.join();
    EXPECT_FALSE(q.TryPop(v));
}

TEST(Queues, SpscCapacityAndPeek) {
    SpscQueue<int> q(4);
    EXPECT_GE(q.Capacity(), 4u);
    for (int i = 0; i < static_cast<int>(q.Capacity()); ++i) EXPECT_TRUE(q.TryPush(i));
    EXPECT_FALSE(q.TryPush(99));
    ASSERT_NE(q.Peek(1), nullptr);
    EXPECT_EQ(*q.Peek(1), 1);
    q.Drop();
    EXPECT_EQ(*q.Peek(0), 1);
}

TEST(Queues, MpscManyProducers) {
    MpscQueue<uint32_t> q(4096);
    constexpr int kThreads = 4, kPer = 100000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < kPer; ++i)
                while (!q.TryPush(static_cast<uint32_t>(t << 24 | i))) std::this_thread::yield();
        });
    std::vector<int> last(kThreads, -1);
    int got = 0;
    uint32_t v;
    while (got < kThreads * kPer) {
        if (!q.TryPop(v)) continue;
        const int t = static_cast<int>(v >> 24), i = static_cast<int>(v & 0xFFFFFF);
        ASSERT_EQ(i, last[t] + 1) << "per-producer order";
        last[t] = i;
        ++got;
    }
    for (auto& t : ts) t.join();
}
