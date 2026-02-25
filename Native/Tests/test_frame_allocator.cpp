#include "test_framework.h"
#include "../Source/Core/FrameAllocator.h"

TEST(FrameAllocator_BasicAlloc) {
    una::FrameAllocator alloc(4096);
    ASSERT_EQ(alloc.capacity(), 4096u);
    ASSERT_EQ(alloc.used(), 0u);

    void* p = alloc.alloc(256);
    ASSERT_TRUE(p != nullptr);
    ASSERT_TRUE(alloc.used() >= 256u);
}

TEST(FrameAllocator_Alignment) {
    una::FrameAllocator alloc(4096);

    // 16-byte alignment
    float* a = alloc.alloc_array<float>(64, 16);
    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(a) % 16, 0u);

    // 32-byte alignment
    float* b = alloc.alloc_array<float>(64, 32);
    ASSERT_TRUE(b != nullptr);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(b) % 32, 0u);
}

TEST(FrameAllocator_Reset) {
    una::FrameAllocator alloc(4096);

    alloc.alloc(1024);
    ASSERT_TRUE(alloc.used() >= 1024u);

    alloc.reset();
    ASSERT_EQ(alloc.used(), 0u);
    ASSERT_EQ(alloc.remaining(), alloc.capacity());

    // Can re-allocate after reset
    void* p = alloc.alloc(2048);
    ASSERT_TRUE(p != nullptr);
}

TEST(FrameAllocator_MultipleAllocs) {
    una::FrameAllocator alloc(4096);

    float* a = alloc.alloc_array<float>(128);
    float* b = alloc.alloc_array<float>(128);
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(a != b);

    // Write to verify no overlap
    for (int i = 0; i < 128; i++) a[i] = 1.0f;
    for (int i = 0; i < 128; i++) b[i] = 2.0f;
    ASSERT_NEAR(a[0], 1.0f, 0.001f);
    ASSERT_NEAR(b[0], 2.0f, 0.001f);
}
