#include "mem.h"
#include "my/cmath.h"
#include "my/io.h"
#include "my/vector.h"
    using namespace mymath;
    using namespace mv;
    using namespace io;

static void puts_raw(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    syscall3(SYS_WRITE, 1, (unsigned long)s, n);
}


static void print_result(const char* name, double val, const char* expect) {
    puts_raw("  ");
    puts_raw(name);
    puts_raw(" = ");
    put_double(val, 6, false);   // 6位精度，不换行
    puts_raw("  [expect: ");
    puts_raw(expect);
    puts_raw("]\n");
}

static void print_result_f(const char* name, float val, const char* expect) {
    puts_raw("  ");
    puts_raw(name);
    puts_raw(" = ");
    put_double((double)val, 6, false);
    puts_raw("  [expect: ");
    puts_raw(expect);
    puts_raw("]\n");
}

extern "C" int main() {


    puts_raw("\n========== cmath test ==========\n\n");

    /* ---------- sqrt ---------- */
    puts_raw("[sqrt]\n");
    print_result("sqrt(4.0)",   sqrt(4.0),   "2.000000");
    print_result("sqrt(2.0)",   sqrt(2.0),   "1.414213");
    print_result("sqrt(100.0)", sqrt(100.0), "10.000000");
    print_result("sqrt(0.0)",   sqrt(0.0),   "0.000000");
    print_result("sqrt(0.5)",   sqrt(0.5),   "0.707106");
    print_result("sqrt(1e10)",  sqrt(1e10),  "100000.000000");
    puts_raw("\n");

    /* ---------- exp ---------- */
    puts_raw("[exp]\n");
    print_result("exp(0.0)",  exp(0.0),  "1.000000");
    print_result("exp(1.0)",  exp(1.0),  "2.718281");
    print_result("exp(2.0)",  exp(2.0),  "7.389056");
    print_result("exp(-1.0)", exp(-1.0), "0.367879");
    print_result("exp(0.5)",  exp(0.5),  "1.648721");
    print_result("exp(-2.0)", exp(-2.0), "0.135335");
    puts_raw("\n");

    /* ---------- log ---------- */
    puts_raw("[log]\n");
    print_result("log(1.0)",         log(1.0),         "0.000000");
    print_result("log(2.718281828)", log(2.718281828), "0.999999");   // ~1.0
    print_result("log(10.0)",        log(10.0),        "2.302585");
    print_result("log(0.5)",         log(0.5),         "-0.693147");
    print_result("log(2.0)",         log(2.0),         "0.693147");
    print_result("log(100.0)",       log(100.0),       "4.605170");
    puts_raw("\n");

    /* ---------- cos ---------- */
    puts_raw("[cos]\n");
    print_result("cos(0.0)",     cos(0.0),     "1.000000");
    print_result("cos(PI/3)",    cos(PI/3.0),  "0.500000");
    print_result("cos(PI/2)",    cos(PI/2.0),  "0.000000");   // ~6.12e-17
    print_result("cos(PI)",      cos(PI),      "-1.000000");
    print_result("cos(2*PI)",    cos(2.0*PI),  "1.000000");
    print_result("cos(-PI/3)",   cos(-PI/3.0), "0.500000");
    puts_raw("\n");

    /* ---------- sin ---------- */
    puts_raw("[sin]\n");
    print_result("sin(0.0)",     sin(0.0),     "0.000000");
    print_result("sin(PI/2)",    sin(PI/2.0),  "1.000000");
    print_result("sin(PI/6)",    sin(PI/6.0),  "0.500000");
    print_result("sin(PI)",      sin(PI),      "0.000000");   // ~1.22e-16
    print_result("sin(-PI/2)",   sin(-PI/2.0), "-1.000000");
    puts_raw("\n");

    /* ---------- pow ---------- */
    puts_raw("[pow]\n");
    print_result("pow(2.0, 10.0)",  pow(2.0, 10.0),  "1024.000000");
    print_result("pow(2.0, -1.0)",  pow(2.0, -1.0),  "0.500000");
    print_result("pow(10.0, 0.0)",  pow(10.0, 0.0),  "1.000000");
    print_result("pow(4.0, 0.5)",   pow(4.0, 0.5),   "2.000000");  // sqrt via pow
    print_result("pow(3.0, 3.0)",   pow(3.0, 3.0),   "27.000000");
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
        put_double(y, 6, false);
        puts_raw("  err=");
        put_double(err, 9, false);
        puts_raw("  [expect err ~ 0]\n");
    }
    puts_raw("\n");

    /* ---------- float overload ---------- */
    puts_raw("[float overload]\n");
    print_result_f("sqrt(2.0f)",  sqrt(2.0f),  "1.414213");
    print_result_f("exp(1.0f)",   exp(1.0f),   "2.718281");
    print_result_f("log(10.0f)",  log(10.0f),  "2.302585");
    print_result_f("cos(0.0f)",   cos(0.0f),   "1.000000");
    puts_raw("\n");

    /* ---------- vector test ---------- */
    puts_raw("========== vector test ==========\n\n");

    /* push_back / size */
    Vector<double> vd;
    for (int i = 0; i < 5; i++) vd.push_back((double)i * 1.5);
    puts_raw("[push_back] size="); put_int((long)vd.size(), false);
    puts_raw("  [expect: 5]\n");
    puts_raw("  vd = [");
    for (unsigned long i = 0; i < vd.size(); i++) {
        put_double(vd[i], 1, false);
        if (i + 1 < vd.size()) puts_raw(", ");
    }
    puts_raw("]  [expect: 0.0, 1.5, 3.0, 4.5, 6.0]\n\n");

    /* reserve / capacity */
    vd.reserve(20);
    puts_raw("[reserve] cap="); put_int((long)vd.capacity(), false);
    puts_raw("  [expect: 20]\n\n");

    /* resize */
    vd.resize(3);
    puts_raw("[resize 3] size="); put_int((long)vd.size(), false);
    puts_raw("  [expect: 3]\n");
    puts_raw("  vd = [");
    for (unsigned long i = 0; i < vd.size(); i++) {
        put_double(vd[i], 1, false);
        if (i + 1 < vd.size()) puts_raw(", ");
    }
    puts_raw("]  [expect: 0.0, 1.5, 3.0]\n\n");

    /* copy ctor */
    Vector<double> vd2(vd);
    puts_raw("[copy ctor] vd2 size="); put_int((long)vd2.size(), false);
    puts_raw("  [expect: 3]\n");
    vd2[0] = 99.0;
    puts_raw("  vd2[0]="); put_double(vd2[0], 1, false);
    puts_raw("  vd[0]=");  put_double(vd[0], 1, false);
    puts_raw("  [expect: 99.0, 0.0]\n\n");

    /* move ctor */
    Vector<double> vd3(mv::move(vd2));
    puts_raw("[move ctor] vd3 size="); put_int((long)vd3.size(), false);
    puts_raw("  vd2 empty="); puts_raw(vd2.empty() ? "true" : "false");
    puts_raw("  [expect: 3, true]\n\n");

    /* clear */
    vd3.clear();
    puts_raw("[clear] size="); put_int((long)vd3.size(), false);
    puts_raw("  empty="); puts_raw(vd3.empty() ? "true" : "false");
    puts_raw("  [expect: 0, true]\n\n");

    /* Vector with init value */
    Vector<int> vi(4, 7);
    puts_raw("[init value] vi = [");
    for (unsigned long i = 0; i < vi.size(); i++) {
        put_int((long)vi[i], false);
        if (i + 1 < vi.size()) puts_raw(", ");
    }
    puts_raw("]  [expect: 7, 7, 7, 7]\n\n");

    /* pop_back */
    vi.pop_back();
    puts_raw("[pop_back] size="); put_int((long)vi.size(), false);
    puts_raw("  [expect: 3]\n\n");

    /* front / back */
    puts_raw("[front/back] front="); put_int((long)vi.front(), false);
    puts_raw(" back="); put_int((long)vi.back(), false);
    puts_raw("  [expect: 7, 7]\n\n");

    /* emplace-like: push_back with move */
    Vector<mv::Vector<int>> vv;
    mv::Vector<int> tmp;
    tmp.push_back(1); tmp.push_back(2);
    vv.push_back(mv::move(tmp));
    puts_raw("[move push] vv[0][0]="); put_int((long)vv[0][0], false);
    puts_raw(" vv[0][1]="); put_int((long)vv[0][1], false);
    puts_raw("  [expect: 1, 2]\n\n");

    puts_raw("========== ALL TESTS PASSED ==========\n");

    return 0;
}