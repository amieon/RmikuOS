

#include "my/stdcompat.h"

extern "C" int main() {
    int errors = 0;

    // std::vector (alias)
    std::vector<int> sv; sv.push_back(10); sv.push_back(20);
    if (sv.size() != 2 || sv[0] != 10) { uprintf("FAIL: std::vector alias\n"); errors++; }

    // std::pair
    std::pair<int, double> pr = std::make_pair(1, 2.5);
    if (pr.first != 1 || pr.second != 2.5) { uprintf("FAIL: std::pair\n"); errors++; }

    // std::tuple
    std::tuple<int, float, char> tp = std::make_tuple(1, 2.5f, 'a');
    auto& [t1, t2, t3] = tp;
    if (t1 != 1 || t2 != 2.5f || t3 != 'a') { uprintf("FAIL: std::tuple/structured binding\n"); errors++; }

    // std::sort
    int arr[] = {9, 3, 7, 1, 5};
    std::sort(arr, arr + 5);
    if (arr[0] != 1 || arr[4] != 9) { uprintf("FAIL: std::sort\n"); errors++; }

    // std::unique
    int arr2[] = {1, 1, 2, 2, 3};
    int* end = std::unique(arr2, arr2 + 5);
    if ((end - arr2) != 3) { uprintf("FAIL: std::unique\n"); errors++; }

    // std::max/min
    if (std::max(3, 5) != 5) { uprintf("FAIL: std::max\n"); errors++; }
    if (std::min(3, 5) != 3) { uprintf("FAIL: std::min\n"); errors++; }

    // std::abs
    if (std::abs(-7) != 7) { uprintf("FAIL: std::abs(int)\n"); errors++; }
    if (std::abs(-3.14) != 3.14) { uprintf("FAIL: std::abs(double)\n"); errors++; }

    // std::move
    std::vector<int> mv1(2, 5); std::vector<int> mv2 = std::move(mv1);
    if (!mv1.empty() || mv2.size() != 2) { uprintf("FAIL: std::move\n"); errors++; }

    // std::mt19937
    std::mt19937 rng(42); std::mt19937 rng2(42);
    if (rng.next() != rng2.next()) { uprintf("FAIL: std::mt19937\n"); errors++; }

    // std::uniform_real_distribution
    std::uniform_real_distribution<double> urd(0.0, 1.0);
    double urv = urd(rng);
    if (urv < 0 || urv >= 1) { uprintf("FAIL: std::uniform_real_distribution\n"); errors++; }

    // std::normal_distribution
    std::normal_distribution<double> nd(0.0, 1.0);
    double nv = nd(rng);
    if (nv < -5 || nv > 5) { uprintf("FAIL: std::normal_distribution\n"); errors++; }

    // std::unordered_map
    std::unordered_map<std::string,int> umap;
    umap["key1"] = 100; umap["key2"] = 200;
    auto it = umap.find("key1");
    if (it == umap.end() || it->second != 100) { uprintf("FAIL: std::unordered_map find\n"); errors++; }
    if (umap.size() != 2) { uprintf("FAIL: std::unordered_map size\n"); errors++; }

    // std::string
    std::string ss("test"); ss += "ing";
    if (ss != std::string("testing")) { uprintf("FAIL: std::string +=\n"); errors++; }

    // std::istringstream
    std::istringstream iss("hello world 42");
    std::string tok1, tok2, tok3;
    iss >> tok1 >> tok2 >> tok3;
    if (tok1 != std::string("hello") || tok2 != std::string("world")) { uprintf("FAIL: std::istringstream\n"); errors++; }
    if (std::stoi(tok3) != 42) { uprintf("FAIL: std::stoi\n"); errors++; }

    // std::is_same
    if (!std::is_same<int, int>::value) { uprintf("FAIL: std::is_same\n"); errors++; }

    // std::conditional
    std::conditional<true, int, float>::type cond_test = 1;
    if (cond_test != 1) { uprintf("FAIL: std::conditional\n"); errors++; }

    uprintf("std_bridge: %d errors\n", errors);
    return errors;
}