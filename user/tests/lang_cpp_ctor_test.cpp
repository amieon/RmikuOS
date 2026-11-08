// C++ 全局 / 函数内 static 对象生命周期回归:
//   .init_array(crt0 遍历) + __cxa_guard_*(一次性构造) + 逆序析构
#include "test.h"

static int g_ev[16];
static int g_n = 0;
static void ev(int x) { if (g_n < 16) g_ev[g_n++] = x; }

/* 终检器最先构造 => 按 C++ 保证最后析构, 且不依赖 GCC 到底用
 * __cxa_atexit 还是 .fini_array 登记全局析构(bare-metal 目标不一定默认前者)。 */
struct Verifier {
    Verifier() { ev(1); }
    ~Verifier() {
        int ok = (g_n == 7 &&
                  g_ev[0] == 1  && g_ev[1] == 11 && g_ev[2] == 21 && g_ev[3] == 31 &&
                  g_ev[4] == 32 && g_ev[5] == 22 && g_ev[6] == 12);
        printf("[%s] 析构逆序 Once -> B -> A -> Verifier (events=%d)\n",
               ok ? "PASS" : "FAIL", g_n);
    }
};

static Verifier g_v;

struct Probe {
    int tag;
    explicit Probe(int t) : tag(t) { ev(t * 10 + 1); }   /* 11 / 21 */
    ~Probe() { ev(tag * 10 + 2); }                       /* 12 / 22 */
};

static Probe g_a(1);
static Probe g_b(2);

static int g_once_calls = 0;
struct Once {
    Once()  { g_once_calls++; ev(31); }
    ~Once() { ev(32); }
};

static Once& once() {
    static Once o;          /* 触发 __cxa_guard_acquire / __cxa_guard_release */
    return o;
}

extern "C" int main() {
    TEST_START("lang_cpp_ctor");

    CHECK(g_n == 3, "三个全局对象在 main 之前已构造");
    CHECK(g_ev[0] == 1 && g_ev[1] == 11 && g_ev[2] == 21,
          "构造顺序 Verifier -> A -> B");

    once(); once(); once();
    CHECK_EQ(g_once_calls, 1, "函数内 static 只构造一次");
    CHECK(g_n == 4 && g_ev[3] == 31, "static 在首次调用时构造");

    TEST_END();
}
