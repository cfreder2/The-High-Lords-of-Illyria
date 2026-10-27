#include <cstdio>

#include "test.h"

int main() {
    using namespace illyria::test;
    for (const Case& c : registry()) {
        const int before = failures();
        c.fn();
        std::printf("%s %s\n", failures() == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu tests, %d failed checks\n", registry().size(), failures());
    return failures() == 0 ? 0 : 1;
}
