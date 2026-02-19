// Single-TU test runner: includes all test files as one compilation unit.
// This ensures all TEST() registrars share the same test_registry.

#include "test_framework.h"

// Pull in all test source files
#include "test_ring_buffer.cpp"
#include "test_frame_allocator.cpp"
#include "test_simd_utils.cpp"
#include "test_pcm_decoder.cpp"
#include "test_mixer.cpp"

int main(int argc, char** argv) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    int ran = 0;
    for (auto& tc : test_registry()) {
        if (filter && tc.name.find(filter) == std::string::npos) continue;

        int before = test_failures();
        std::printf("[ RUN  ] %s\n", tc.name.c_str());
        tc.func();
        if (test_failures() == before) {
            std::printf("[ PASS ] %s\n", tc.name.c_str());
        } else {
            std::printf("[ FAIL ] %s\n", tc.name.c_str());
        }
        ran++;
    }

    std::printf("\n%d test(s) run, %d assertion(s), %d failure(s)\n",
                ran, test_assertions(), test_failures());
    return test_failures() > 0 ? 1 : 0;
}
