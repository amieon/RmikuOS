// gason (MIT, upstream c9c4aa6) 解析正确性回归。
// gason.h / gason.cpp 原样引入未改动；本文件只负责调用与断言。
// 宿主对拍：用 target/vendor/host_stub.h 替换 test.h 后可原样编译，输出应逐字节一致。
#include "test.h"
#include "gason.h"

static int key_eq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static JsonValue find_key(JsonValue obj, const char* key) {
    for (auto n : obj) {
        if (n->key && key_eq(n->key, key)) return n->value;
    }
    return JsonValue();
}

static int count_array(JsonValue arr) {
    int n = 0;
    for (auto e : arr) { (void)e; n++; }
    return n;
}

static double first_number(JsonValue arr) {
    for (auto e : arr) return e->value.toNumber();
    return 0;
}

/* gason 会就地改写输入，必须是可写数组，不能传字符串字面量。 */
static char bad0[] = "{\"a\":[1,2}";
static char bad1[] = "{\"a\":tru}";
static char bad2[] = "{\"a\":\"\\q\"}";

extern "C" int main() {
    TEST_START("lang_cpp_gason");

    char buf[] = "{\"a\":[1,2,-3.5],\"b\":{\"c\":true},\"d\":null,\"s\":\"hi\"}";
    JsonAllocator alloc;
    JsonValue root;
    char* end = 0;
    int err = jsonParse(buf, &end, &root, alloc);
    CHECK(err == JSON_OK, "parse ok");
    CHECK(root.getTag() == JSON_OBJECT, "root is object");

    JsonValue a = find_key(root, "a");
    CHECK(a.getTag() == JSON_ARRAY, "a is array");
    CHECK(count_array(a) == 3, "a has 3 elements");
    CHECK(first_number(a) == 1.0, "a[0] == 1");

    JsonValue b = find_key(root, "b");
    CHECK(b.getTag() == JSON_OBJECT, "b is object");
    CHECK(find_key(b, "c").getTag() == JSON_TRUE, "b.c is true");
    CHECK(find_key(root, "d").getTag() == JSON_NULL, "d is null");

    JsonValue s = find_key(root, "s");
    CHECK(s.getTag() == JSON_STRING && key_eq(s.toString(), "hi"), "s == hi");

    char* bad[3] = { bad0, bad1, bad2 };
    for (int i = 0; i < 3; i++) {
        JsonAllocator a2;
        JsonValue v2;
        char* e2 = 0;
        int r = jsonParse(bad[i], &e2, &v2, a2);
        printf("[DUMP] bad[%d] err=%d msg=%s\n", i, r, jsonStrError(r));
        if (i == 0)      CHECK(r != JSON_OK, "bad0 rejected");
        else if (i == 1) CHECK(r != JSON_OK, "bad1 rejected");
        else             CHECK(r != JSON_OK, "bad2 rejected");
    }

    TEST_END();
}
