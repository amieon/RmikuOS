#include "types.h"
#include "classfile.h"
#include "heap.h"
#include "interp.h"
#include "native.h"
#include "my/stdcompat.h"

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "Main.class";
    FILE* f = fopen(path, "rb");
    if (!f) { printf("fopen"); return 1; }
    static uint8_t buf[65536];
    int len = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    VM vm;
    ClassFile cf = parse_class(buf);
    vm.main_class = &cf;
    vm.classes[cf.cp_class_name(cf.this_class)] = &cf;

    // load super if not Object (simplified: assume Object has no fields/methods)
    if (cf.super_class != 0) {
        std::string sname = cf.cp_class_name(cf.super_class);
        if (sname != "java/lang/Object" && vm.classes.count(sname) == 0) {
            // would need to load .class file here
        }
    }

    register_natives(vm);

    Method* m = cf.find_method("main", "([Ljava/lang/String;)V");
    if (!m) { fprintf(stderr, "no main\n"); return 1; }

    Object* args_arr = vm.heap.alloc_array(0, T_REF);
    std::vector<Value> args;
    args.push_back(Value::fromRef(args_arr));
    vm.exec(m, &cf, args);

    if (vm.exception_obj) {
        fprintf(stderr, "Uncaught exception: %s\n", vm.exception_obj->hack_str.c_str());
        return 1;
    }
    return 0;
}
