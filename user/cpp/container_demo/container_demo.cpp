#include "container_demo.h"

void test_vector() {
    std::vector<int> v;
    for (int i = 0; i < 5; i++) v.push_back(i * i);

    printf("vector: [");
    for (size_t i = 0; i < v.size(); i++) {
        printf("%d", v[i]);
        if (i + 1 < v.size()) printf(", ");
    }
    printf("]\n");
}

void test_string() {
    std::string s1("hello");
    std::string s2 = s1 + std::string(" world");

    printf("string: %s (len=%lu)\n", s2.c_str(), (unsigned long)s2.size());

    if (s2 == std::string("hello world")) {
        printf("string compare: PASS\n");
    } else {
        printf("string compare: FAIL\n");
    }
}

void test_pair_tuple() {
    std::pair<int, double> p = std::make_pair(42, 3.14);
    printf("pair: (%d, %f)\n", p.first, p.second);

    std::tuple<int, float, char> t = std::make_tuple(1, 2.5f, 'a');
    auto& [a, b, c] = t;
    printf("tuple: (%d, %f, %c)\n", a, b, c);
}

void test_sort_shuffle() {
    int arr[] = {5, 2, 8, 1, 9, 3};
    std::sort(arr, arr + 6);
    printf("sorted: [");
    for (int i = 0; i < 6; i++) {
        printf("%d", arr[i]);
        if (i + 1 < 6) printf(", ");
    }
    printf("]\n");

    // shuffle
    std::mt19937 rng(42);
    std::shuffle(arr, arr + 6, rng);
    printf("shuffled: [");
    for (int i = 0; i < 6; i++) {
        printf("%d", arr[i]);
        if (i + 1 < 6) printf(", ");
    }
    printf("]\n");
}
