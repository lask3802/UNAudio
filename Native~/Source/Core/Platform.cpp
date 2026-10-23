#include "Platform.h"

#include <thread>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <avrt.h>
#elif defined(__APPLE__)
#  include <mach/mach_time.h>
#  include <dispatch/dispatch.h>
#  include <pthread.h>
#else
#  include <time.h>
#  include <semaphore.h>
#  include <pthread.h>
#  include <errno.h>
#  include <sched.h>
#  include <sys/resource.h>
#  include <unistd.h>
#  if defined(__linux__) || defined(__ANDROID__)
#    include <sys/syscall.h>
#  endif
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  include <xmmintrin.h>
#  include <pmmintrin.h>
#endif

namespace una {

// ── Host time ─────────────────────────────────────────────────────

#if defined(_WIN32)
namespace {
struct QpcInfo {
    int64_t freq;
    QpcInfo() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
    }
};
const QpcInfo& Qpc() {
    static QpcInfo info;
    return info;
}
} // namespace

int64_t HostTimeNs() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const int64_t f = Qpc().freq;
    // Split to avoid overflow of c * 1e9.
    const int64_t sec = c.QuadPart / f;
    const int64_t rem = c.QuadPart % f;
    return sec * 1000000000LL + rem * 1000000000LL / f;
}
#elif defined(__APPLE__)
int64_t HostTimeNs() {
    static mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t;
        mach_timebase_info(&t);
        return t;
    }();
    const uint64_t t = mach_absolute_time();
    if (tb.numer == tb.denom) return static_cast<int64_t>(t);
    return static_cast<int64_t>((__uint128_t)t * tb.numer / tb.denom);
}
#else
int64_t HostTimeNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}
#endif

// ── Denormals ─────────────────────────────────────────────────────

void EnableFlushToZero() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__) && !defined(_MSC_VER)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24); // FZ
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__arm__) && !defined(_MSC_VER) && defined(__ARM_FP)
    uint32_t fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1U << 24);
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(fpscr));
#endif
}

// ── Thread helpers ────────────────────────────────────────────────

void SetCurrentThreadRole(ThreadRole role) {
#if defined(_WIN32)
    if (role == ThreadRole::AudioRealtime) {
        DWORD taskIndex = 0;
        HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        if (h) AvSetMmThreadPriority(h, AVRT_PRIORITY_CRITICAL);
        else SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    } else if (role == ThreadRole::StreamService) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    } else {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    }
#elif defined(__APPLE__)
    // The CoreAudio IO thread is already real-time and never passes through
    // here; AudioRealtime comes from our own paced threads (the null device).
    if (role == ThreadRole::AudioRealtime || role == ThreadRole::StreamService)
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    else if (role == ThreadRole::Decoder)
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#else
    if (role == ThreadRole::AudioRealtime) {
        sched_param sp{};
        sp.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
#  if defined(__linux__) || defined(__ANDROID__)
            setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), -16);
#  endif
        }
    } else {
#  if defined(__linux__) || defined(__ANDROID__)
        int nice = role == ThreadRole::StreamService ? -8 : 5;
        setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), nice);
#  endif
    }
#endif
}

void SetCurrentThreadName(const char* name) {
#if defined(_WIN32)
    wchar_t wname[64];
    int i = 0;
    for (; name[i] && i < 63; ++i) wname[i] = static_cast<wchar_t>(name[i]);
    wname[i] = 0;
    using SetDesc = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static SetDesc fn = reinterpret_cast<SetDesc>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (fn) fn(GetCurrentThread(), wname);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#else
    char buf[16];
    int i = 0;
    for (; name[i] && i < 15; ++i) buf[i] = name[i];
    buf[i] = 0;
    pthread_setname_np(pthread_self(), buf);
#endif
}

// ── Semaphore ─────────────────────────────────────────────────────

#if defined(_WIN32)
Semaphore::Semaphore() { handle_ = CreateSemaphoreW(nullptr, 0, 0x7fffffff, nullptr); }
Semaphore::~Semaphore() { if (handle_) CloseHandle(static_cast<HANDLE>(handle_)); }
void Semaphore::Signal() { ReleaseSemaphore(static_cast<HANDLE>(handle_), 1, nullptr); }
bool Semaphore::Wait(int timeoutMs) {
    return WaitForSingleObject(static_cast<HANDLE>(handle_),
                               timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs)) == WAIT_OBJECT_0;
}
#elif defined(__APPLE__)
Semaphore::Semaphore() { handle_ = (void*)dispatch_semaphore_create(0); }
Semaphore::~Semaphore() {
    if (handle_) dispatch_release((dispatch_semaphore_t)handle_);
}
void Semaphore::Signal() { dispatch_semaphore_signal((dispatch_semaphore_t)handle_); }
bool Semaphore::Wait(int timeoutMs) {
    dispatch_time_t t = timeoutMs < 0 ? DISPATCH_TIME_FOREVER
                                      : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeoutMs * 1000000LL);
    return dispatch_semaphore_wait((dispatch_semaphore_t)handle_, t) == 0;
}
#else
Semaphore::Semaphore() {
    auto* s = new sem_t;
    sem_init(s, 0, 0);
    handle_ = s;
}
Semaphore::~Semaphore() {
    auto* s = static_cast<sem_t*>(handle_);
    sem_destroy(s);
    delete s;
}
void Semaphore::Signal() { sem_post(static_cast<sem_t*>(handle_)); }
bool Semaphore::Wait(int timeoutMs) {
    auto* s = static_cast<sem_t*>(handle_);
    if (timeoutMs < 0) {
        while (sem_wait(s) != 0 && errno == EINTR) {}
        return true;
    }
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeoutMs / 1000;
    ts.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
    int r;
    while ((r = sem_timedwait(s, &ts)) != 0 && errno == EINTR) {}
    return r == 0;
}
#endif

// ── Precise sleep ─────────────────────────────────────────────────

void SleepUntilNs(int64_t hostNs) {
    constexpr int64_t kSpinNs = 200000;  // finish the last 0.2 ms with yields
    int64_t left = hostNs - HostTimeNs();
    if (left > kSpinNs) {
#if defined(_WIN32)
        // Default Sleep granularity is up to 15.6 ms; a high-resolution waitable
        // timer (Windows 10 1803+) wakes within ~0.5 ms without timeBeginPeriod.
        thread_local HANDLE timer = [] {
            HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
            return h ? h : CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }();
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -((left - kSpinNs) / 100);  // relative, 100 ns units
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
        } else {
            Sleep(static_cast<DWORD>((left - kSpinNs) / 1000000));
        }
#elif defined(__APPLE__)
        const int64_t target = hostNs - kSpinNs;
        while ((left = target - HostTimeNs()) > 0) {
            timespec ts{static_cast<time_t>(left / 1000000000LL), static_cast<long>(left % 1000000000LL)};
            nanosleep(&ts, nullptr);
        }
#else
        const int64_t target = hostNs - kSpinNs;
        timespec ts{static_cast<time_t>(target / 1000000000LL), static_cast<long>(target % 1000000000LL)};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
#endif
    }
    while (HostTimeNs() < hostNs) std::this_thread::yield();
}

} // namespace una
