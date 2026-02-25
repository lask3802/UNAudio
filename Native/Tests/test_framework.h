#ifndef UNAUDIO_TEST_FRAMEWORK_H
#define UNAUDIO_TEST_FRAMEWORK_H

// Minimal test framework — no external dependencies, header-only.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& test_failures() { static int f = 0; return f; }
inline int& test_assertions() { static int a = 0; return a; }

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> func) {
        test_registry().push_back({name, std::move(func)});
    }
};

#define TEST(name)                                             \
    static void test_##name();                                 \
    static TestRegistrar reg_##name(#name, test_##name);       \
    static void test_##name()

#define ASSERT_TRUE(expr)                                      \
    do {                                                       \
        test_assertions()++;                                   \
        if (!(expr)) {                                         \
            std::fprintf(stderr, "  FAIL: %s:%d: %s\n",       \
                         __FILE__, __LINE__, #expr);           \
            test_failures()++;                                 \
        }                                                      \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b)                                        \
    do {                                                       \
        test_assertions()++;                                   \
        if ((a) != (b)) {                                      \
            std::fprintf(stderr, "  FAIL: %s:%d: %s != %s\n", \
                         __FILE__, __LINE__, #a, #b);          \
            test_failures()++;                                 \
        }                                                      \
    } while (0)

#define ASSERT_NEAR(a, b, eps)                                 \
    do {                                                       \
        test_assertions()++;                                   \
        if (std::fabs(static_cast<double>(a) -                 \
                      static_cast<double>(b)) > (eps)) {       \
            std::fprintf(stderr,                               \
                "  FAIL: %s:%d: |%s - %s| > %s  (%f vs %f)\n",\
                __FILE__, __LINE__, #a, #b, #eps,              \
                static_cast<double>(a), static_cast<double>(b));\
            test_failures()++;                                 \
        }                                                      \
    } while (0)

#endif // UNAUDIO_TEST_FRAMEWORK_H
