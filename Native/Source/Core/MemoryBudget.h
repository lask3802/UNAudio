#ifndef UNAUDIO_MEMORY_BUDGET_H
#define UNAUDIO_MEMORY_BUDGET_H

/*
 * Memory budget tracker for audio assets.
 * Thread-safe via atomics — can be queried from any thread.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace una {

struct MemoryBudgetConfig {
    size_t max_compressed_bytes = 64 * 1024 * 1024;  // 64 MB
    size_t max_decoded_bytes    = 8  * 1024 * 1024;  // 8 MB
    float  warning_threshold    = 0.85f;              // 85%
};

struct MemoryUsage {
    size_t compressed_bytes;
    size_t decoded_bytes;
    size_t total_bytes;
    float  compressed_percent;
    float  decoded_percent;
};

class MemoryBudget {
public:
    void configure(const MemoryBudgetConfig& cfg) { config_ = cfg; }

    bool try_alloc_compressed(size_t bytes) {
        size_t current = compressed_.load(std::memory_order_relaxed);
        size_t desired = current + bytes;
        if (desired > config_.max_compressed_bytes)
            return false;
        while (!compressed_.compare_exchange_weak(current, desired,
                   std::memory_order_relaxed)) {
            desired = current + bytes;
            if (desired > config_.max_compressed_bytes)
                return false;
        }
        return true;
    }

    void free_compressed(size_t bytes) {
        compressed_.fetch_sub(bytes, std::memory_order_relaxed);
    }

    bool try_alloc_decoded(size_t bytes) {
        size_t current = decoded_.load(std::memory_order_relaxed);
        size_t desired = current + bytes;
        if (desired > config_.max_decoded_bytes)
            return false;
        while (!decoded_.compare_exchange_weak(current, desired,
                   std::memory_order_relaxed)) {
            desired = current + bytes;
            if (desired > config_.max_decoded_bytes)
                return false;
        }
        return true;
    }

    void free_decoded(size_t bytes) {
        decoded_.fetch_sub(bytes, std::memory_order_relaxed);
    }

    MemoryUsage get_usage() const {
        MemoryUsage u;
        u.compressed_bytes = compressed_.load(std::memory_order_relaxed);
        u.decoded_bytes    = decoded_.load(std::memory_order_relaxed);
        u.total_bytes      = u.compressed_bytes + u.decoded_bytes;
        u.compressed_percent = config_.max_compressed_bytes > 0
            ? static_cast<float>(u.compressed_bytes) / config_.max_compressed_bytes
            : 0.0f;
        u.decoded_percent = config_.max_decoded_bytes > 0
            ? static_cast<float>(u.decoded_bytes) / config_.max_decoded_bytes
            : 0.0f;
        return u;
    }

    bool is_warning() const {
        auto u = get_usage();
        return u.compressed_percent >= config_.warning_threshold ||
               u.decoded_percent    >= config_.warning_threshold;
    }

    const MemoryBudgetConfig& config() const { return config_; }

private:
    MemoryBudgetConfig config_;
    std::atomic<size_t> compressed_{0};
    std::atomic<size_t> decoded_{0};
};

} // namespace una

#endif // UNAUDIO_MEMORY_BUDGET_H
