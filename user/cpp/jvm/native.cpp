#include "interp.h"
#include "native.h"
#include "heap.h"
#include "my/stdcompat.h"

static void print_int(int32_t v) {
    char buf[32]; int len=0;
    if (v<0) { buf[len++]='-'; v=-v; }
    char tmp[32]; int t=0;
    do { tmp[t++]='0'+v%10; v/=10; } while(v);
    while(t) buf[len++]=tmp[--t];
    buf[len++]='\n';
    write(1,buf,len);
}

void register_natives(VM& vm) {
    vm.natives["print(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        print_int(args[0].asInt()); return Value();
    };
    vm.natives["java/io/PrintStream.println(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        print_int(args[0].asInt()); return Value();
    };
    vm.natives["java/io/PrintStream.println(Ljava/lang/String;)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        if (args[0].obj) {
            const std::string& s = args[0].obj->hack_str;
            write(1, s.c_str(), s.size());
            write(1, "\n", 1);
        } else {
            write(1, "null\n", 5);
        }
        return Value();
    };
    vm.natives["java/lang/System.exit(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        _exit(args[0].asInt());
        return Value();
    };
    vm.natives["java/lang/Object.<init>()V"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value();
    };
    vm.natives["printString(Ljava/lang/String;)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        if (args[0].obj) {
            const std::string& s = args[0].obj->hack_str;
            write(1, s.c_str(), s.size());
            write(1, "\n", 1);
        } else {
            write(1, "null\n", 5);
        }
        return Value();
    };
}

Value call_native(VM& vm, const std::string& cls, const std::string& name,
                  const std::string& desc, std::vector<Value>& args) {
    std::string key = cls + "." + name + desc;
    auto it = vm.natives.find(key);
    if (it != vm.natives.end()) return it->second(vm, args);
    // fallback: try just name+desc
    key = name + desc;
    it = vm.natives.find(key);
    if (it != vm.natives.end()) return it->second(vm, args);
    fprintf(stderr, "Unsatisfied native: %s.%s%s\n", cls.c_str(), name.c_str(), desc.c_str());
    _exit(1);
}
