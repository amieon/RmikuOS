#include "mem.h"
#include "my/cmath.h"
#include "my/io.h"
#include "my/vector.h"
using namespace io;
using namespace mymath;


static void puts_raw(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    syscall3(SYS_WRITE, 1, (unsigned long)s, n);
}

static void print_result(const char* name, double val, const char* expect) {
    puts_raw("  ");
    puts_raw(name);
    puts_raw(" = ");
    put_double(val, 12, false);
    puts_raw("  [expect: ");
    puts_raw(expect);
    puts_raw("]\n");
}

extern "C" int main() {
    using namespace mymath;

    puts_raw("\n========== cmath test (fdlibm) ==========\n\n");

    /* ---------- sqrt ---------- */
    puts_raw("[sqrt]\n");
    print_result("sqrt(4.0)",   sqrt(4.0),   "2.000000000000");
    print_result("sqrt(2.0)",   sqrt(2.0),   "1.414213562373");
    print_result("sqrt(100.0)", sqrt(100.0), "10.000000000000");
    print_result("sqrt(0.0)",   sqrt(0.0),   "0.000000000000");
    print_result("sqrt(0.5)",   sqrt(0.5),   "0.707106781186");
    puts_raw("\n");

    /* ---------- exp ---------- */
    puts_raw("[exp]\n");
    print_result("exp(0.0)",  exp(0.0),  "1.000000000000");
    print_result("exp(1.0)",  exp(1.0),  "2.718281828459");
    print_result("exp(2.0)",  exp(2.0),  "7.389056098930");
    print_result("exp(-1.0)", exp(-1.0), "0.367879441171");
    print_result("exp(0.5)",  exp(0.5),  "1.648721270700");
    print_result("exp(-2.0)", exp(-2.0), "0.135335283236");
    puts_raw("\n");

    /* ---------- log ---------- */
    puts_raw("[log]\n");
    print_result("log(1.0)",         log(1.0),         "0.000000000000");
    print_result("log(2.718281828)", log(2.718281828), "0.999999999989"); // ~1.0
    print_result("log(10.0)",        log(10.0),        "2.302585092994");
    print_result("log(0.5)",         log(0.5),         "-0.693147180559");
    print_result("log(2.0)",         log(2.0),         "0.693147180559");
    print_result("log(100.0)",       log(100.0),       "4.605170185988");
    puts_raw("\n");

    /* ---------- round-trip (softmax/CE 关键) ---------- */
    puts_raw("[round-trip: log(exp(x)) == x]\n");
    double rt_vals[] = {0.5, 1.0, 2.0, 3.0, 5.0, -1.0, -3.0, 0.0};
    for (int i = 0; i < 8; i++) {
        double x = rt_vals[i];
        double y = log(exp(x));
        double err = y - x;
        puts_raw("  log(exp(");
        put_double(x, 1, false);
        puts_raw(")) = ");
        put_double(y, 12, false);
        puts_raw("  err=");
        put_double(err, 15, false);
        puts_raw("  [expect err ~ 0]\n");
    }
    puts_raw("\n");

    /* ---------- pow ---------- */
    puts_raw("[pow]\n");
    print_result("pow(2.0, 10.0)",  pow(2.0, 10.0),  "1024.000000000000");
    print_result("pow(2.0, -1.0)",  pow(2.0, -1.0),  "0.500000000000");
    print_result("pow(10.0, 0.0)",  pow(10.0, 0.0),  "1.000000000000");
    print_result("pow(4.0, 0.5)",   pow(4.0, 0.5),   "2.000000000000");
    print_result("pow(3.0, 3.0)",   pow(3.0, 3.0),   "27.000000000000");
    puts_raw("\n");

    puts_raw("========== ALL TESTS DONE ==========\n");
    return 0;
}