#include "interp.h"
#include "native.h"
#include "heap.h"
#include "my/stdcompat.h"
#include "lock.h"
#include "process.h"
#include "net.h"
#include "thread.h"

static void print_int(int32_t v) {
    char buf[32]; int len=0;
    if (v<0) { buf[len++]='-'; v=-v; }
    char tmp[32]; int t=0;
    do { tmp[t++]='0'+v%10; v/=10; } while(v);
    while(t) buf[len++]=tmp[--t];
    buf[len++]='\n';
    write(1,buf,len);
}

static void print_jstr(Object* o) {
    if (o) {
        const std::string& s = o->hack_str;
        write(1, s.c_str(), s.size());
        write(1, "\n", 1);
    } else {
        write(1, "null\n", 5);
    }
}

static const char* val_str(Value& v) {
    return v.obj ? v.obj->hack_str.c_str() : "";
}

// ================= Mem: malloc/free 句柄表 =================
// Java 拿不到裸指针（JVM 没有 long 运算），用 int 句柄索引 native 侧的分配表
static std::vector<void*> g_mem_ptrs;
static std::vector<usize> g_mem_sizes;
static mutex_t g_mem_lock = MUTEX_INIT;

static int mem_alloc_handle(usize size) {
    void* p = malloc(size);
    if (!p) return -1;
    mutex_lock(&g_mem_lock);
    for (size_t i = 0; i < g_mem_ptrs.size(); i++) {
        if (!g_mem_ptrs[i]) {
            g_mem_ptrs[i] = p;
            g_mem_sizes[i] = size;
            mutex_unlock(&g_mem_lock);
            return (int)i + 1;
        }
    }
    g_mem_ptrs.push_back(p);
    g_mem_sizes.push_back(size);
    int h = (int)g_mem_ptrs.size();
    mutex_unlock(&g_mem_lock);
    return h;
}

// 返回 nullptr 表示句柄/偏移非法
static void* mem_resolve(int h, int off, int need) {
    if (h <= 0 || (size_t)h > g_mem_ptrs.size()) return nullptr;
    mutex_lock(&g_mem_lock);
    void* p = g_mem_ptrs[h - 1];
    usize sz = g_mem_sizes[h - 1];
    mutex_unlock(&g_mem_lock);
    if (!p || off < 0 || (usize)(off + need) > sz) return nullptr;
    return (char*)p + off;
}

// ================= Thread: 每个 Java 线程跑一个独立的小 JVM =================
// 堆/帧各自独立，类存储全局共享（load_class 里已加锁），对象不跨线程传递
struct ThreadBoot {
    char* classpath;
    char* class_name;
    int arg;
};

static char* dup_cstr(const char* s) {
    if (!s) return nullptr;
    usize n = 0;
    while (s[n]) n++;
    char* p = (char*)malloc(n + 1);
    for (usize i = 0; i <= n; i++) p[i] = s[i];
    return p;
}

static void jvm_thread_entry(void* p) {
    ThreadBoot* b = (ThreadBoot*)p;
    int code = 0;

    VM vm;
    vm.classpath = b->classpath ? b->classpath : "";
    register_natives(vm);

    ClassFile* cf = load_class(vm, b->classpath, b->class_name);
    if (!cf) {
        printf("[jvm-thread] class not found: %s\n", b->class_name);
        code = 1;
    } else {
        // 约定：线程入口是 public static void run(int arg)
        Method* m = cf->find_method("run", "(I)V");
        if (!m || !m->is_static()) {
            printf("[jvm-thread] %s needs: public static void run(int arg)\n", b->class_name);
            code = 1;
        } else {
            std::vector<Value> args;
            args.push_back(Value::fromInt(b->arg));
            vm.invoke(m, cf, args);
            if (vm.exception_obj) code = 1;
        }
    }

    free(b->classpath);
    free(b->class_name);
    free(b);
    thread_exit(code);
}

void register_natives(VM& vm) {
    // ---------- 旧的兼容 native（各 demo 类里自己声明的那些） ----------
    vm.natives["print(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        print_int(args[0].asInt()); return Value();
    };
    vm.natives["java/io/PrintStream.println(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        print_int(args[0].asInt()); return Value();
    };
    vm.natives["java/io/PrintStream.println(Ljava/lang/String;)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        print_jstr(args[0].obj); return Value();
    };
    vm.natives["java/lang/System.exit(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        _exit(args[0].asInt());
        return Value();
    };
    vm.natives["java/lang/Object.<init>()V"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value();
    };
    auto println_string = [](VM& vm, std::vector<Value>& args)->Value {
        print_jstr(args[0].obj); return Value();
    };
    vm.natives["printString(Ljava/lang/String;)V"] = println_string;
    vm.natives["println(Ljava/lang/String;)V"] = println_string;

    // ================= Rmiku$IO：文件 + 控制台 =================
    vm.natives["Rmiku$IO.printInt(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        print_int(args[0].asInt()); return Value();
    };
    vm.natives["Rmiku$IO.printStr(Ljava/lang/String;)V"] = println_string;

    vm.natives["Rmiku$IO.open(Ljava/lang/String;I)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value::fromInt((int)open(val_str(args[0]), (usize)args[1].asInt()));
    };
    vm.natives["Rmiku$IO.create(Ljava/lang/String;)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        // O_WRONLY|O_CREAT|O_TRUNC，覆盖写
        return Value::fromInt((int)open_create(val_str(args[0]), O_WRONLY | O_TRUNC));
    };
    vm.natives["Rmiku$IO.close(I)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value::fromInt((int)close(args[0].asInt()));
    };
    vm.natives["Rmiku$IO.read(I[B)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        Object* arr = args[1].obj;
        if (!arr || arr->array_length <= 0) return Value::fromInt(-1);
        char tmp[1024];
        int want = arr->array_length < 1024 ? arr->array_length : 1024;
        isize n = read(args[0].asInt(), tmp, (usize)want);
        if (n <= 0) return Value::fromInt((int32_t)n);
        for (int i = 0; i < n; i++) arr->fields[i] = Value::fromInt((int32_t)(int8_t)tmp[i]);
        return Value::fromInt((int32_t)n);
    };
    vm.natives["Rmiku$IO.write(I[BI)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        Object* arr = args[1].obj;
        if (!arr) return Value::fromInt(-1);
        int len = args[2].asInt();
        if (len > arr->array_length) len = arr->array_length;
        char tmp[1024];
        int done = 0;
        while (done < len) {
            int chunk = len - done < 1024 ? len - done : 1024;
            for (int i = 0; i < chunk; i++) tmp[i] = (char)arr->fields[done + i].asInt();
            isize n = write(args[0].asInt(), tmp, (usize)chunk);
            if (n <= 0) return Value::fromInt(done > 0 ? done : (int32_t)n);
            done += (int)n;
        }
        return Value::fromInt(done);
    };
    vm.natives["Rmiku$IO.writeStr(ILjava/lang/String;)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        if (!args[1].obj) return Value::fromInt(-1);
        const std::string& s = args[1].obj->hack_str;
        return Value::fromInt((int)write(args[0].asInt(), s.c_str(), s.size()));
    };
    vm.natives["Rmiku$IO.readAll(Ljava/lang/String;)Ljava/lang/String;"] = [](VM& vm, std::vector<Value>& args)->Value {
        int fd = (int)open(val_str(args[0]), O_RDONLY);
        if (fd < 0) return Value::fromRef(nullptr);
        std::string out;
        char tmp[512];
        isize n;
        while ((n = read(fd, tmp, 512)) > 0)
            for (int i = 0; i < n; i++) out.push_back(tmp[i]);
        close(fd);
        return Value::fromRef(vm.heap.alloc_string(out.c_str()));
    };
    vm.natives["Rmiku$IO.writeAll(Ljava/lang/String;Ljava/lang/String;)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        int fd = (int)open_create(val_str(args[0]), O_WRONLY | O_TRUNC);
        if (fd < 0) return Value::fromInt(-1);
        int total = 0;
        if (args[1].obj) {
            const std::string& s = args[1].obj->hack_str;
            total = (int)write(fd, s.c_str(), s.size());
        }
        close(fd);
        return Value::fromInt(total);
    };
    vm.natives["Rmiku$IO.readChar()I"] = [](VM& vm, std::vector<Value>& args)->Value {
        char c;
        return Value::fromInt(read(0, &c, 1) == 1 ? (int32_t)(uint8_t)c : -1);
    };
    vm.natives["Rmiku$IO.readLine()Ljava/lang/String;"] = [](VM& vm, std::vector<Value>& args)->Value {
        std::string out;
        char c;
        while (read(0, &c, 1) == 1) {
            if (c == '\n') break;
            out.push_back(c);
        }
        return Value::fromRef(vm.heap.alloc_string(out.c_str()));
    };

    // ================= Rmiku$Mem：手动内存 =================
    vm.natives["Rmiku$Mem.malloc(I)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        int size = args[0].asInt();
        if (size <= 0) return Value::fromInt(-1);
        return Value::fromInt(mem_alloc_handle((usize)size));
    };
    vm.natives["Rmiku$Mem.free(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        int h = args[0].asInt();
        if (h <= 0 || (size_t)h > g_mem_ptrs.size()) return Value();
        mutex_lock(&g_mem_lock);
        void* p = g_mem_ptrs[h - 1];
        g_mem_ptrs[h - 1] = nullptr;
        g_mem_sizes[h - 1] = 0;
        mutex_unlock(&g_mem_lock);
        free(p);
        return Value();
    };
    vm.natives["Rmiku$Mem.load8(II)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        void* p = mem_resolve(args[0].asInt(), args[1].asInt(), 1);
        if (!p) return Value::fromInt(-1);
        return Value::fromInt((int32_t)*(uint8_t*)p);
    };
    vm.natives["Rmiku$Mem.store8(III)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        void* p = mem_resolve(args[0].asInt(), args[1].asInt(), 1);
        if (p) *(uint8_t*)p = (uint8_t)args[2].asInt();
        return Value();
    };
    vm.natives["Rmiku$Mem.load32(II)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        // 按字节拼，避免非对齐访问（小端）
        uint8_t* p = (uint8_t*)mem_resolve(args[0].asInt(), args[1].asInt(), 4);
        if (!p) return Value::fromInt(-1);
        uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        return Value::fromInt((int32_t)v);
    };
    vm.natives["Rmiku$Mem.store32(III)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        uint8_t* p = (uint8_t*)mem_resolve(args[0].asInt(), args[1].asInt(), 4);
        if (p) {
            uint32_t v = (uint32_t)args[2].asInt();
            p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
            p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
        }
        return Value();
    };

    // ================= Rmiku$Proc：进程 =================
    vm.natives["Rmiku$Proc.fork()I"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value::fromInt((int32_t)fork());
    };
    vm.natives["Rmiku$Proc.waitpid(I)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        int st = 0;
        isize r = waitpid(args[0].asInt(), &st, 0);
        if (r < 0) return Value::fromInt(-1);
        return Value::fromInt((int32_t)WEXITSTATUS(st));
    };
    vm.natives["Rmiku$Proc.getpid()I"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value::fromInt((int32_t)getpid());
    };
    vm.natives["Rmiku$Proc.sleep(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        sleep((usize)args[0].asInt()); return Value();
    };
    vm.natives["Rmiku$Proc.yield()V"] = [](VM& vm, std::vector<Value>& args)->Value {
        yield(); return Value();
    };
    vm.natives["Rmiku$Proc.exit(I)V"] = [](VM& vm, std::vector<Value>& args)->Value {
        exit(args[0].asInt());
        return Value();
    };

    // ================= Rmiku$Thread：用户态线程 =================
    vm.natives["Rmiku$Thread.spawn(Ljava/lang/String;I)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        ThreadBoot* b = (ThreadBoot*)malloc(sizeof(ThreadBoot));
        if (!b) return Value::fromInt(-1);
        b->classpath = vm.classpath.empty() ? nullptr : dup_cstr(vm.classpath.c_str());
        b->class_name = dup_cstr(val_str(args[0]));
        b->arg = args[1].asInt();
        int tid = (int)thread_create(jvm_thread_entry, b);
        if (tid < 0) {
            free(b->classpath);
            free(b->class_name);
            free(b);
        }
        return Value::fromInt(tid);
    };
    vm.natives["Rmiku$Thread.join(I)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        int code = 0;
        int r = (int)thread_join(args[0].asInt(), &code);
        return Value::fromInt(r < 0 ? -1 : code);
    };

    // ================= Rmiku$Net：网络 =================
    // ip 用 int 表示（主机序），如 10.0.2.2 = 0x0A000202
    vm.natives["Rmiku$Net.udpSocket()I"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value::fromInt(socket_udp());
    };
    vm.natives["Rmiku$Net.tcpSocket()I"] = [](VM& vm, std::vector<Value>& args)->Value {
        return Value::fromInt(socket_tcp());
    };
    vm.natives["Rmiku$Net.bind(II)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        struct sockaddr_in a = addr_of(0, (unsigned short)args[1].asInt());
        return Value::fromInt(bind(args[0].asInt(), &a, sizeof(a)));
    };
    vm.natives["Rmiku$Net.connect(III)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        struct sockaddr_in a = addr_of((unsigned int)args[1].asInt(), (unsigned short)args[2].asInt());
        return Value::fromInt(connect(args[0].asInt(), &a, sizeof(a)));
    };
    vm.natives["Rmiku$Net.send(ILjava/lang/String;)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        if (!args[1].obj) return Value::fromInt(-1);
        const std::string& s = args[1].obj->hack_str;
        return Value::fromInt(send(args[0].asInt(), s.c_str(), (int)s.size(), 0));
    };
    vm.natives["Rmiku$Net.sendTo(ILjava/lang/String;II)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        if (!args[1].obj) return Value::fromInt(-1);
        const std::string& s = args[1].obj->hack_str;
        struct sockaddr_in a = addr_of((unsigned int)args[2].asInt(), (unsigned short)args[3].asInt());
        return Value::fromInt(sendto(args[0].asInt(), s.c_str(), (int)s.size(), 0, &a, sizeof(a)));
    };
    vm.natives["Rmiku$Net.recv(I[B)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        Object* arr = args[1].obj;
        if (!arr || arr->array_length <= 0) return Value::fromInt(-1);
        char tmp[1500];
        int want = arr->array_length < 1500 ? arr->array_length : 1500;
        int n = recv(args[0].asInt(), tmp, want, 0);
        if (n <= 0) return Value::fromInt(n);
        for (int i = 0; i < n; i++) arr->fields[i] = Value::fromInt((int32_t)(int8_t)tmp[i]);
        return Value::fromInt(n);
    };
    vm.natives["Rmiku$Net.recvFrom(I[B)I"] = [](VM& vm, std::vector<Value>& args)->Value {
        Object* arr = args[1].obj;
        if (!arr || arr->array_length <= 0) return Value::fromInt(-1);
        char tmp[1500];
        int want = arr->array_length < 1500 ? arr->array_length : 1500;
        int n = recvfrom(args[0].asInt(), tmp, want, 0, nullptr, nullptr);
        if (n <= 0) return Value::fromInt(n);
        for (int i = 0; i < n; i++) arr->fields[i] = Value::fromInt((int32_t)(int8_t)tmp[i]);
        return Value::fromInt(n);
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