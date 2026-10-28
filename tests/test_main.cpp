#include <cstdio>
#include <cstring>

#include "test.h"

namespace {
// Runs the tests whose names contain `filter` (all of them for null).
int run(const char* filter) {
    using namespace illyria::test;
    size_t ran = 0;
    for (const Case& c : registry()) {
        if (filter != nullptr && std::strstr(c.name, filter) == nullptr) continue;
        const int before = failures();
        c.fn();
        ++ran;
        std::printf("%s %s\n", failures() == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu tests, %d failed checks\n", ran, failures());
    return failures() == 0 ? 0 : 1;
}
}  // namespace

#ifdef __EMSCRIPTEN__
// This Emscripten's runtime never calls a main that takes arguments.
int main() { return run(nullptr); }
#else
// An argument runs only the tests whose names contain it.
int main(int argc, char** argv) { return run(argc > 1 ? argv[1] : nullptr); }
#endif
