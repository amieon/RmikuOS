#include "math_utils.h"

extern "C" int main() {
    io::puts("=== math_demo (C multi-file project) ===\n");

    print_result("factorial", 5, factorial(5));   // 120
    print_result("factorial", 10, factorial(10)); // 3628800
    print_result("fibonacci", 10, fibonacci(10)); // 55
    print_result("fibonacci", 20, fibonacci(20)); // 6765

    io::puts("=== done ===\n");
    return 0;
}
