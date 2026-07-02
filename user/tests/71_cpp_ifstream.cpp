#include "my/stdcompat.h"

extern "C" int main() {
    uprintf("=== ifstream operator>> chain test ===\n");
    
    // 测试：模拟 Dataset.h 的用法
    std::ifstream fe;
    std::string a, b;
    
    // 这行就是 Dataset.h 里报错的
    // while (fe >> a >> b) {
    //     uprintf("read: %s %s\n", a.c_str(), b.c_str());
    // }
    
    // 先测试单步
    fe >> a;
    uprintf("single op OK\n");
    
    // 再测试链式
    fe >> a >> b;
    uprintf("chain op OK\n");
    
    uprintf("=== done ===\n");
    return 0;
}