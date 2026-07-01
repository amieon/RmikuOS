#include "my/stdcompat.h"

extern "C" int main() {
    int errors = 0;

    // 构造
    std::vector<int> v1; if (!v1.empty()) { uprintf("FAIL: empty ctor\n"); errors++; }
    std::vector<int> v2(5); if (v2.size() != 5) { uprintf("FAIL: size ctor\n"); errors++; }
    std::vector<int> v3(3, 7); if (v3[0] != 7 || v3[2] != 7) { uprintf("FAIL: init ctor\n"); errors++; }

    // push_back / pop_back
    std::vector<int> v4; v4.push_back(1); v4.push_back(2); v4.push_back(3);
    if (v4.size() != 3 || v4.back() != 3) { uprintf("FAIL: push_back\n"); errors++; }
    v4.pop_back(); if (v4.size() != 2 || v4.back() != 2) { uprintf("FAIL: pop_back\n"); errors++; }

    // reserve / capacity
    v4.reserve(20); if (v4.capacity() < 20) { uprintf("FAIL: reserve\n"); errors++; }

    // resize
    v4.resize(10); if (v4.size() != 10 || v4[5] != 0) { uprintf("FAIL: resize default\n"); errors++; }
    v4.resize(2); if (v4.size() != 2) { uprintf("FAIL: resize shrink\n"); errors++; }

    // copy ctor
    std::vector<int> v5(v4); if (v5.size() != 2 || v5[0] != v4[0]) { uprintf("FAIL: copy ctor\n"); errors++; }
    v5[0] = 99; if (v4[0] == 99) { uprintf("FAIL: copy independence\n"); errors++; }

    // move ctor
    std::vector<int> v6(std::move(v5)); if (v6.size() != 2 || !v5.empty()) { uprintf("FAIL: move ctor\n"); errors++; }

    // emplace_back
    std::vector<mv::Pair<int,int>> vp; vp.emplace_back(1, 2);
    if (vp.size() != 1 || vp[0].first != 1 || vp[0].second != 2) { uprintf("FAIL: emplace_back\n"); errors++; }

    // clear
    v6.clear(); if (!v6.empty()) { uprintf("FAIL: clear\n"); errors++; }

    // front/back
    std::vector<int> v7(2, 5); if (v7.front() != 5 || v7.back() != 5) { uprintf("FAIL: front/back\n"); errors++; }

    // iterator
    int sum = 0; for (auto it = v7.begin(); it != v7.end(); ++it) sum += *it;
    if (sum != 10) { uprintf("FAIL: iterator\n"); errors++; }

    uprintf("vector_v2: %d errors\n", errors);
    return errors;
}