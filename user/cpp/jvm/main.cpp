#include "types.h"
#include "classfile.h"
#include "heap.h"
#include "interp.h"
#include "native.h"
#include "my/stdcompat.h"
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

ClassFile* load_class(VM& vm, const char* classpath, const char* classname) {
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

    // 递归加载父类
    if (cf->super_class != 0) {
        std::string sname = cf->cp_class_name(cf->super_class);
        if (sname != "java/lang/Object" && !vm.classes.count(sname)) {
            load_class(vm, classpath, sname.c_str());
        }
    }
    return cf;
}

int main(int argc, char** argv) {
    // 用法：jvm <classpath> <MainClass>
    // 或：jvm <MainClass.class>   （兼容旧用法）
    const char* classpath = nullptr;
    const char* main_name = "Main";

    if (argc > 2) {
        classpath = argv[1];
        main_name = argv[2];
    } else if (argc > 1) {
        // 尝试判断是目录还是 .class 文件
        int len = 0;
        while (argv[1][len]) len++;
        if (len > 6 && argv[1][len-6] == '.' && argv[1][len-5] == 'c' &&
            argv[1][len-4] == 'l' && argv[1][len-3] == 'a' &&
            argv[1][len-2] == 's' && argv[1][len-1] == 's') {
            // 直接加载单个 .class 文件（旧模式）
            static uint8_t buf[65536];
            int flen = 0;
            if (!read_file(argv[1], buf, &flen)) {
                printf("Cannot open %s\n", argv[1]);
                return 1;
            }
            VM vm;
            ClassFile cf = parse_class(buf);
            vm.main_class = &cf;
            vm.classes[cf.cp_class_name(cf.this_class)] = &cf;
            register_natives(vm);
            Method* m = cf.find_method("main", "([Ljava/lang/String;)V");
            if (!m) { printf("no main\n"); return 1; }
            Object* args_arr = vm.heap.alloc_array(0, T_REF);
            std::vector<Value> args;
            args.push_back(Value::fromRef(args_arr));
            vm.exec(m, &cf, args);
            if (vm.exception_obj) {
                printf("Uncaught exception\n");
                return 1;
            }
            return 0;
        } else {
            // 当作 classpath，默认 Main 类
            classpath = argv[1];
        }
    }

    VM vm;
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
    vm.exec(m, main_cf, args);

    if (vm.exception_obj) {
        printf("Uncaught exception\n");
        return 1;
    }
    return 0;
}