#include "types.h"
#include "classfile.h"
#include "heap.h"
#include "interp.h"
#include "native.h"
#include "my/stdcompat.h"
#include "lock.h"
#include "aot.h"

extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*destructor)(void*), void* arg, void* dso) {
        (void)destructor; (void)arg; (void)dso;
        return 0;  // 直接忽略析构，裸机进程结束直接回收
    }
}

static const int MAX_CLASSES = 32;
static ClassFile g_class_storage[MAX_CLASSES];
static int g_class_count = 0;


static bool read_file(const char* path, uint8_t* buf, int* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    int len = 0;
    while (len < 65536) {
        int n = fread(buf + len, 1, 65536 - len, f);
        if (n <= 0) break;
        len += n;
    }
    fclose(f);
    *out_len = len;
    return true;
}

static void build_path(char* out, const char* classpath, const char* classname) {
    // classpath 是目录，如 "/jvm/HelloWorld"
    // classname 如 "Point" 或 "com/example/Point" 或 "Main$Inner"
    int i = 0;
    if (classpath) {
        while (*classpath) out[i++] = *classpath++;
        out[i++] = '/';
    }
    while (*classname) {
        char c = *classname++;
        out[i++] = (c == '.') ? '/' : c;  // 包名分隔符 . -> /
    }
    out[i++] = '.'; out[i++] = 'c'; out[i++] = 'l'; out[i++] = 'a';
    out[i++] = 's'; out[i++] = 's'; out[i] = '\0';
}

// 类存储是全局共享的（g_class_storage/g_class_count），
// 多线程各自跑独立 JVM 时会并发加载类，这里加锁保护。
// 注意：递归加载父类要走 load_class_locked，不能再次拿锁。
static mutex_t g_load_lock = MUTEX_INIT;

static ClassFile* load_class_locked(VM& vm, const char* classpath, const char* classname) {
    // 已加载？
    std::string name = classname;
    if (vm.classes.count(name)) return vm.classes[name];
    if (g_class_count >= MAX_CLASSES) {
        printf("Too many classes\n");
        return nullptr;
    }

    char path[256];
    build_path(path, classpath, classname);

    static uint8_t buf[65536];
    int len = 0;
    if (!read_file(path, buf, &len)) {
        printf("Class not found: %s (tried %s)\n", classname, path);
        return nullptr;
    }

    ClassFile* cf = &g_class_storage[g_class_count++];
    *cf = parse_class(buf);
    vm.classes[cf->cp_class_name(cf->this_class)] = cf;

    // 加载父类，并接上 super 链（resolve_method 要靠它向上找方法）
    if (cf->super_class != 0) {
        std::string sname = cf->cp_class_name(cf->super_class);
        if (sname == "java/lang/Object") {
            // 合成一个最小的 java/lang/Object：只有一个 native <init>
            if (!vm.classes.count(sname)) {
                if (g_class_count >= MAX_CLASSES) { printf("Too many classes\n"); return nullptr; }
                ClassFile* obj = &g_class_storage[g_class_count++];
                *obj = ClassFile();
                obj->cp.resize(3);
                obj->cp[1].tag = 1; obj->cp[1].str = "java/lang/Object";
                obj->cp[2].tag = 7; obj->cp[2].u.class_name_idx = 1;
                obj->this_class = 2;
                Method init;
                init.flags = 0x0100; // native
                init.name = "<init>";
                init.desc = "()V";
                obj->methods.push_back(init);
                vm.classes[sname] = obj;
            }
            cf->super = vm.classes[sname];
        } else {
            cf->super = load_class_locked(vm, classpath, sname.c_str());
        }
    }
    // 装载期 AOT：把本类的方法编译成本机机器码（失败的方法走解释器）
    aot_compile_class(cf);
    return cf;
}

ClassFile* load_class(VM& vm, const char* classpath, const char* classname) {
    mutex_lock(&g_load_lock);
    ClassFile* r = load_class_locked(vm, classpath, classname);
    mutex_unlock(&g_load_lock);
    return r;
}

int main(int argc, char** argv) {
    // 用法：jvm [classpath] <MainClass>
    //   jvm Main          —— 从当前目录加载 Main.class（以及它引用的其他类）
    //   jvm Main.class    —— 同上，兼容带后缀的写法
    //   jvm /jvm demo.Main —— 指定 classpath，包名用 . 分隔
    if (argc < 2) {
        printf("usage: jvm [classpath] <MainClass>\n");
        return 1;
    }

    const char* classpath = nullptr;  // nullptr = 当前目录
    char name_buf[256];
    char dir_buf[256];
    if (argc > 2) {
        classpath = argv[1];
        int i = 0;
        while (argv[2][i] && i < 255) { name_buf[i] = argv[2][i]; i++; }
        name_buf[i] = '\0';
    } else {
        int i = 0;
        while (argv[1][i] && i < 255) { name_buf[i] = argv[1][i]; i++; }
        name_buf[i] = '\0';
        // 兼容 jvm Xxx.class：去掉 .class 后缀，统一走 load_class
        int len = i;
        if (len > 6 && name_buf[len-6] == '.' && name_buf[len-5] == 'c' &&
            name_buf[len-4] == 'l' && name_buf[len-3] == 'a' &&
            name_buf[len-2] == 's' && name_buf[len-1] == 's') {
            name_buf[len-6] = '\0';
        }
        // 带路径的写法（如 jvm mem_demo/MemDemo.class）：
        // 目录部分当 classpath，否则它引用的同目录类会懒加载失败
        int slash = -1;
        for (int j = 0; name_buf[j]; j++) {
            if (name_buf[j] == '/') slash = j;
        }
        if (slash >= 0) {
            for (int j = 0; j < slash; j++) dir_buf[j] = name_buf[j];
            dir_buf[slash] = '\0';
            int k = 0;
            for (int j = slash + 1; name_buf[j]; j++) name_buf[k++] = name_buf[j];
            name_buf[k] = '\0';
            classpath = dir_buf;
        }
    }
    const char* main_name = name_buf;

    VM vm;
    vm.classpath = classpath ? classpath : "";
    ClassFile* main_cf = load_class(vm, classpath, main_name);
    if (!main_cf) {
        printf("Cannot load main class: %s\n", main_name);
        return 1;
    }
    vm.main_class = main_cf;
    register_natives(vm);

    Method* m = main_cf->find_method("main", "([Ljava/lang/String;)V");
    if (!m) { printf("no main\n"); return 1; }

    Object* args_arr = vm.heap.alloc_array(0, T_REF);
    std::vector<Value> args;
    args.push_back(Value::fromRef(args_arr));
    vm.invoke(m, main_cf, args);

    if (vm.exception_obj) {
        printf("Uncaught exception\n");
        return 1;
    }
    return 0;
}