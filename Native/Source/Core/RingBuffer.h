#ifndef UNAUDIO_RING_BUFFER_H
#define UNAUDIO_RING_BUFFER_H

/*
 * Lock-free SPSC (Single Producer, Single Consumer) ring buffer.
 * Used for decode thread → audio thread PCM data transfer,
 * and for command/event queues between threads.
 *
 * Zero allocation after construction, cache-line friendly.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace una {

template <typename T, size_t Capacity>
class RingBuffer {
    static_assert(std::is_trivially_copyable<T>::value,
                  "RingBuffer only supports trivially copyable types");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    RingBuffer() : head_(0), tail_(0) {}

    // Producer: try to push one element. Returns false if full.
    bool try_push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & MASK;
        if (next == tail_.load(std::memory_order_acquire))
            return false; // full
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Producer: push a batch of items. Returns number actually pushed.
    size_t try_push_batch(const T* items, size_t count) {
        size_t pushed = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!try_push(items[i])) break;
            ++pushed;
        }
        return pushed;
    }

    // Consumer: try to pop one element. Returns false if empty.
    bool try_pop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false; // empty
        item = buffer_[tail];
        tail_.store((tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    // Consumer: pop up to max_count elements into dst. Returns count popped.
    size_t try_pop_batch(T* dst, size_t max_count) {
        size_t popped = 0;
        for (size_t i = 0; i < max_count; ++i) {
            if (!try_pop(dst[i])) break;
            ++popped;
        }
        return popped;
    }

    size_t size() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & MASK;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    bool full() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return ((h + 1) & MASK) == t;
    }

    static constexpr size_t capacity() { return Capacity - 1; }

    void reset() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr size_t MASK = Capacity - 1;

    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    T buffer_[Capacity];
};

} // namespace una

#endif // UNAUDIO_RING_BUFFER_H
