#pragma once
// Host clock, thread helpers and a lightweight semaphore.
// Everything here is safe to call from the audio thread unless noted.

#include <cstdint>
#include <atomic>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#elif defined(_MSC_VER) && defined(_M_ARM64)
#include <intrin.h>
#endif

namespace una {

/// Monotonic host time in nanoseconds (QPC / mach_absolute_time / CLOCK_MONOTONIC).
int64_t HostTimeNs();

/// Block until HostTimeNs() >= hostNs with sub-millisecond accuracy (not for
/// device callback threads; used by self-paced threads such as the Null device).
void SleepUntilNs(int64_t hostNs);

/// Flush denormals to zero for the calling thread (SSE FTZ/DAZ, ARM FZ).
void EnableFlushToZero();

enum class ThreadRole { AudioRealtime, StreamService, Decoder };

/// Best-effort priority boost for the calling thread. Not for the device
/// callback thread on platforms that already run it real-time (CoreAudio, AAudio).
void SetCurrentThreadRole(ThreadRole role);

/// Set the calling thread's debugger name (best effort).
void SetCurrentThreadName(const char* name);

/// Counting semaphore whose Signal() is safe to call from the audio thread:
/// it is a single syscall that never blocks (ReleaseSemaphore / sem_post /
/// dispatch_semaphore_signal).
class Semaphore {
public:
    Semaphore();
    ~Semaphore();
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    void Signal();
    /// Returns true if signalled, false on timeout.
    bool Wait(int timeoutMs);

private:
    void* handle_ = nullptr;
};

inline void CpuRelax() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  if defined(_MSC_VER)
    _mm_pause();
#  else
    __builtin_ia32_pause();
#  endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#  if defined(_MSC_VER)
    __yield();
#  else
    __asm__ __volatile__("yield");
#  endif
#endif
}

} // namespace una

