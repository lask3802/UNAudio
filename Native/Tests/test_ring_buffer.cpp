#include "test_framework.h"
#include "../Source/Core/RingBuffer.h"
#include "../Source/Core/CommandQueue.h"
#include "../Source/Core/EventQueue.h"

TEST(RingBuffer_PushPop) {
    una::RingBuffer<int, 16> rb;

    ASSERT_TRUE(rb.empty());
    ASSERT_EQ(rb.size(), 0u);

    ASSERT_TRUE(rb.try_push(42));
    ASSERT_FALSE(rb.empty());
    ASSERT_EQ(rb.size(), 1u);

    int val = 0;
    ASSERT_TRUE(rb.try_pop(val));
    ASSERT_EQ(val, 42);
    ASSERT_TRUE(rb.empty());
}

TEST(RingBuffer_Full) {
    una::RingBuffer<int, 4> rb; // capacity 4

    ASSERT_TRUE(rb.try_push(1));
    ASSERT_TRUE(rb.try_push(2));
    ASSERT_TRUE(rb.try_push(3));
    // One slot is sentinel, so only 3 elements fit
    ASSERT_FALSE(rb.try_push(4));

    int val;
    ASSERT_TRUE(rb.try_pop(val));
    ASSERT_EQ(val, 1);
    ASSERT_TRUE(rb.try_push(4)); // now has space
}

TEST(RingBuffer_Batch) {
    una::RingBuffer<int, 64> rb;

    int data[] = {10, 20, 30, 40, 50};
    size_t pushed = rb.try_push_batch(data, 5);
    ASSERT_EQ(pushed, 5u);
    ASSERT_EQ(rb.size(), 5u);

    int out[5] = {};
    size_t popped = rb.try_pop_batch(out, 5);
    ASSERT_EQ(popped, 5u);
    ASSERT_EQ(out[0], 10);
    ASSERT_EQ(out[4], 50);
}

TEST(RingBuffer_FIFO_Order) {
    una::RingBuffer<int, 32> rb;
    for (int i = 0; i < 10; ++i)
        ASSERT_TRUE(rb.try_push(i * 100));

    for (int i = 0; i < 10; ++i) {
        int val;
        ASSERT_TRUE(rb.try_pop(val));
        ASSERT_EQ(val, i * 100);
    }
}

TEST(CommandQueue_BasicUsage) {
    una::CommandQueue cq;

    una::AudioCommand cmd{};
    cmd.type = una::AudioCommand::Type::Play;
    cmd.voice_id = 5;
    ASSERT_TRUE(cq.try_push(cmd));

    una::AudioCommand out{};
    ASSERT_TRUE(cq.try_pop(out));
    ASSERT_EQ(static_cast<int>(out.type), static_cast<int>(una::AudioCommand::Type::Play));
    ASSERT_EQ(out.voice_id, 5);
}

TEST(EventQueue_BasicUsage) {
    una::EventQueue eq;

    una::AudioEvent evt{};
    evt.type = una::AudioEvent::Type::VoiceFinished;
    evt.voice_id = 3;
    ASSERT_TRUE(eq.try_push(evt));

    una::AudioEvent out{};
    ASSERT_TRUE(eq.try_pop(out));
    ASSERT_EQ(static_cast<int>(out.type),
              static_cast<int>(una::AudioEvent::Type::VoiceFinished));
    ASSERT_EQ(out.voice_id, 3);
}
