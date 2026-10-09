// SPDX-License-Identifier: GPL-2.0+
// Minimal test helpers (no framework dependency).
#pragma once

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace testing {
inline int &failures() {
    static int n = 0;
    return n;
}
inline int &checks() {
    static int n = 0;
    return n;
}
inline void fail(const char *file, int line, const std::string &what) {
    ++failures();
    std::fprintf(stderr, "%s:%d: FALHOU: %s\n", file, line, what.c_str());
}
inline int report(const char *name) {
    std::printf("%s: %d verificações, %d falhas\n", name, checks(), failures());
    return failures() ? 1 : 0;
}
template <class A, class B> std::string pair(const A &a, const B &b) {
    std::ostringstream s;
    s << a << " != " << b;
    return s.str();
}
} // namespace testing

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++testing::checks();                                                                                           \
        if (!(cond)) testing::fail(__FILE__, __LINE__, #cond);                                                         \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        ++testing::checks();                                                                                           \
        if (!((a) == (b))) testing::fail(__FILE__, __LINE__, #a " == " #b ": " + testing::pair((a), (b)));             \
    } while (0)
#define CHECK_NEAR(a, b, tol)                                                                                          \
    do {                                                                                                               \
        ++testing::checks();                                                                                           \
        if (!(std::fabs((double)(a) - (double)(b)) <= (tol)))                                                          \
            testing::fail(__FILE__, __LINE__, #a " ~= " #b ": " + testing::pair((a), (b)));                            \
    } while (0)
