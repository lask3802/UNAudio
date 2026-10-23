#pragma once
// Lock-free single-producer / single-consumer ring with runtime capacity.
// Storage is allocated once in the constructor; push/pop never allocate.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>

namespace una {

template <typename T>
class SpscQueue {
    static_assert(std::is_trivially_copyable<T>::value, "SpscQueue needs trivially copyable T");

public:
    explicit SpscQueue(size_t minCapacity) {
        size_t cap = 2;
        while (cap < minCapacity + 1) cap <<= 1;
        mask_ = cap - 1;
        items_.reset(new T[cap]);
    }

    bool TryPush(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tailCache_) {
            tailCache_ = tail_.load(std::memory_order_acquire);
            if (next == tailCache_) return false;
        }
        items_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool TryPop(T& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == headCache_) {
            headCache_ = head_.load(std::memory_order_acquire);
            if (tail == headCache_) return false;
        }
        out = items_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    /// Consumer-side: pointer to the i-th unread element, or null.
    T* Peek(size_t i = 0) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        if (((head - tail) & mask_) <= i) return nullptr;
        return &items_[(tail + i) & mask_];
    }

    /// Consumer-side: drop the oldest element (after Peek).
    void Drop() {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
    }

    size_t SizeApprox() const {
        return (head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire)) & mask_;
    }

    size_t Capacity() const { return mask_; }

private:
    alignas(64) std::atomic<size_t> head_{0};
    size_t tailCache_ = 0; // producer-local
    alignas(64) std::atomic<size_t> tail_{0};
    size_t headCache_ = 0; // consumer-local
    alignas(64) size_t mask_ = 0;
    std::unique_ptr<T[]> items_;
};

/// Multi-producer / single-consumer: producers serialize on a mutex,
/// the consumer (audio thread) stays lock-free.
template <typename T>
class MpscQueue {
public:
    explicit MpscQueue(size_t capacity) : q_(capacity) {}

    bool TryPush(const T& item) {
        std::lock_guard<std::mutex> lock(producerMutex_);
        return q_.TryPush(item);
    }

    bool TryPop(T& out) { return q_.TryPop(out); }
    size_t SizeApprox() const { return q_.SizeApprox(); }

private:
    std::mutex producerMutex_;
    SpscQueue<T> q_;
};

} // namespace una
