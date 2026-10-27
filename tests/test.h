#pragma once

// Minimal self-registering test harness; keeps the core free of third-party
// dependencies so it builds identically for native and WebAssembly targets.

#include <cstdio>
#include <functional>
#include <vector>

namespace illyria::test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

}  // namespace illyria::test

#define ILLYRIA_CONCAT_(a, b) a##b
#define ILLYRIA_CONCAT(a, b) ILLYRIA_CONCAT_(a, b)

#define TEST(name)                                                                    \
    static void name();                                                               \
    static ::illyria::test::Registrar ILLYRIA_CONCAT(registrar_, name)(#name, name); \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);  \
            ++::illyria::test::failures();                                           \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                          \
    do {                                                                                 \
        const double a_ = (actual), e_ = (expected);                                     \
        if (!(a_ - e_ <= (tolerance) && e_ - a_ <= (tolerance))) {                       \
            std::printf("  %s:%d: %s == %.6f, expected %.6f +/- %g\n", __FILE__, __LINE__, \
                        #actual, a_, e_, static_cast<double>(tolerance));                \
            ++::illyria::test::failures();                                               \
        }                                                                                \
    } while (0)
