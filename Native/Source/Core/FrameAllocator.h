#ifndef UNAUDIO_FRAME_ALLOCATOR_H
#define UNAUDIO_FRAME_ALLOCATOR_H

/*
 * Bump allocator for the audio thread.
 * Guarantees O(1) allocation with no syscalls, no locks, no fragmentation.
 * The entire arena is reset at the beginning of each audio callback.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cassert>

namespace una {

class FrameAllocator {
public:
    explicit FrameAllocator(size_t capacity)
        : capacity_((capacity + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1)), offset_(0)
    {
        // std::aligned_alloc requires size to be a multiple of alignment
#if defined(_MSC_VER)
        base_ = static_cast<uint8_t*>(_aligned_malloc(capacity_, ARENA_ALIGN));
#else
        base_ = static_cast<uint8_t*>(std::aligned_alloc(ARENA_ALIGN, capacity_));
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

    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    // O(1) aligned allocation — NO syscall, NO lock.
    // Returns nullptr on OOM in both debug and release builds.
    // Callers (e.g. AudioMixer::Process) MUST handle nullptr with a
    // heap fallback.
    void* alloc(size_t bytes, size_t align = 16) {
        size_t aligned_offset = (offset_ + align - 1) & ~(align - 1);
        if (aligned_offset + bytes > capacity_) {
            assert(false && "FrameAllocator: out of memory");
            oomCount_++;
            return nullptr;
        }
        void* ptr = base_ + aligned_offset;
        offset_ = aligned_offset + bytes;
        return ptr;
    }

    /// Number of OOM events since last reset (diagnostic counter).
    size_t oom_count() const { return oomCount_; }

    template <typename T>
    T* alloc_array(size_t count, size_t align = 16) {
        return static_cast<T*>(alloc(sizeof(T) * count, align));
    }

    void reset() { offset_ = 0; oomCount_ = 0; }

    size_t capacity()  const { return capacity_; }
    size_t used()      const { return offset_; }
    size_t remaining() const { return capacity_ - offset_; }

private:
    static constexpr size_t ARENA_ALIGN = 32; // AVX2-friendly

    uint8_t* base_;
    size_t   capacity_;
    size_t   offset_;
    size_t   oomCount_ = 0;
};

} // namespace una

#endif // UNAUDIO_FRAME_ALLOCATOR_H
