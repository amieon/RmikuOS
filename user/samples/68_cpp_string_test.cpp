

#include "my/stdcompat.h"

extern "C" int main() {
    int errors = 0;

    // mystr::strcmp
    if (mystr::strcmp("abc", "abc") != 0) { printf("FAIL: strcmp equal\n"); errors++; }
    if (mystr::strcmp("abc", "abd") >= 0) { printf("FAIL: strcmp less\n"); errors++; }
    if (mystr::strcmp("abd", "abc") <= 0) { printf("FAIL: strcmp greater\n"); errors++; }

    // mystr::str_to_int
    if (mystr::str_to_int("123") != 123) { printf("FAIL: str_to_int positive\n"); errors++; }
    if (mystr::str_to_int("-456") != -456) { printf("FAIL: str_to_int negative\n"); errors++; }
    if (mystr::str_to_int("0") != 0) { printf("FAIL: str_to_int zero\n"); errors++; }

    // mystr::str_to_double
    double d1 = mystr::str_to_double("3.14159");
    if (d1 < 3.141 || d1 > 3.142) { printf("FAIL: str_to_double (got %f)\n", d1); errors++; }
    double d2 = mystr::str_to_double("-0.5");
    if (d2 != -0.5) { printf("FAIL: str_to_double negative\n"); errors++; }

    // split_line
    char line[] = "hello world 42 3.14";
    char* tokens[16];
    int nt = mystr::split_line(line, tokens, 16);
    if (nt != 4) { printf("FAIL: split_line count (got %d)\n", nt); errors++; }
    if (mystr::strcmp(tokens[0], "hello") != 0) { printf("FAIL: split_line token0\n"); errors++; }
    if (mystr::strcmp(tokens[3], "3.14") != 0) { printf("FAIL: split_line token3\n"); errors++; }

    // SimpleMap
    mystr::SimpleMap<int> smap;
    smap["key1"] = 100;
    smap["key2"] = 200;
    int* v1 = smap.find("key1");
    if (!v1 || *v1 != 100) { printf("FAIL: SimpleMap find\n"); errors++; }
    int* v2 = smap.find("nonexist");
    if (v2 != nullptr) { printf("FAIL: SimpleMap miss\n"); errors++; }
    if (smap.size() != 2) { printf("FAIL: SimpleMap size\n"); errors++; }

    // std::string
    std::string s1("hello");
    if (s1.size() != 5) { printf("FAIL: string size\n"); errors++; }
    if (mystr::strcmp(s1.c_str(), "hello") != 0) { printf("FAIL: string c_str\n"); errors++; }

    std::string s2 = s1;
    if (s2 != s1) { printf("FAIL: string copy\n"); errors++; }

    s2 += " world";
    if (s2.size() != 11) { printf("FAIL: string +=\n"); errors++; }

    std::string s3 = s1 + std::string("!");
    if (s3 != std::string("hello!")) { printf("FAIL: string +\n"); errors++; }

    std::string s4; s4 = "direct";
    if (s4 != std::string("direct")) { printf("FAIL: string = const char*\n"); errors++; }

    printf("string: %d errors\n", errors);
    return errors;
}