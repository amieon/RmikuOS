#include "container_demo.h"

extern "C" int main() {
    uprintf("=== container_demo (C++ multi-file project) ===\n");

    test_vector();
    test_string();
    test_pair_tuple();
    test_sort_shuffle();

    uprintf("=== all tests done ===\n");
    return 0;
}
