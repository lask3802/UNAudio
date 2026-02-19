#pragma once
/*
 * UNAudio — Frame Allocator
 *
 * Bump allocator for the audio thread. Guarantees O(1) allocation with no
 * syscalls, no locks, and no fragmentation. The entire arena is reset at
 * the beginning of each audio callback frame.
 *
 * Usage:
 *   FrameAllocator alloc(256 * 1024);          // 256 KB arena
 *   alloc.reset();                              // top of audio callback
 *   float* buf = alloc.alloc_array<float>(512); // 512 floats, 16-byte aligned
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cassert>

namespace una {

class FrameAllocator {
public:
    explicit FrameAllocator(size_t capacity)
        : capacity_(capacity), offset_(0)
    {
#if defined(_MSC_VER)
        base_ = static_cast<uint8_t*>(_aligned_malloc(capacity, ARENA_ALIGN));
#else
        base_ = static_cast<uint8_t*>(std::aligned_alloc(ARENA_ALIGN, capacity));
#endif
        assert(base_ && "FrameAllocator: allocation failed");
    }

    ~FrameAllocator()
    {
#if defined(_MSC_VER)
        _aligned_free(base_);
#else
        std::free(base_);
#endif
    }

    // Non-copyable, non-movable
    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    // O(1) aligned allocation — NO syscall, NO lock
    void* alloc(size_t bytes, size_t align = 16)
    {
        size_t aligned_offset = (offset_ + align - 1) & ~(align - 1);
        if (aligned_offset + bytes > capacity_) {
            assert(false && "FrameAllocator: out of memory");
            return nullptr;
        }
        void* ptr = base_ + aligned_offset;
        offset_ = aligned_offset + bytes;
        return ptr;
    }

    // Typed array allocation with SIMD-friendly alignment
    template <typename T>
    T* alloc_array(size_t count, size_t align = 16)
    {
        return static_cast<T*>(alloc(sizeof(T) * count, align));
    }

    // Reset the entire arena (call at top of each audio callback)
    void reset() { offset_ = 0; }

    // Query
    size_t capacity()  const { return capacity_; }
    size_t used()      const { return offset_; }
    size_t remaining() const { return capacity_ - offset_; }

private:
    static constexpr size_t ARENA_ALIGN = 32; // AVX2-friendly

    uint8_t* base_;
    size_t   capacity_;
    size_t   offset_;
};

} // namespace una
