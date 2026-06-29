#include "../include/mem.h"
#include "../include/my/vector.h"

// extern "C" {
//     long syscall3(unsigned long id, unsigned long a0, unsigned long a1, unsigned long a2);
// }
// static const unsigned long SYS_EXIT  = 0;
// static const unsigned long SYS_WRITE = 2;

extern "C" { long syscall3(unsigned long,unsigned long,unsigned long,unsigned long); }
static void puts_raw(const char* s){unsigned long n=0;while(s[n])n++;syscall3(2,1,(unsigned long)s,n);}
static void put_int(long v){char b[24];int n=0;if(v==0)b[n++]='0';char t[24];int k=0;while(v>0){t[k++]=char('0'+v%10);v/=10;}while(k>0)b[n++]=t[--k];b[n++]='\n';b[n]=0;puts_raw(b);}

extern "C" int main() {
    puts_raw("myvec test\n");
    mv::Vector<int> v;
    for (int i = 1; i <= 100; i++) v.push_back(i);
    long sum = 0;
    for (int x : v) sum += x;     // 范围 for 用 begin/end
    puts_raw("sum = "); put_int(sum);     // 5050
    puts_raw("size = "); put_int((long)v.size());  // 100
    syscall3(0,0,0,0);
    return 0;
}