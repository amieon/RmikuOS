#include "user.h"
int main(){

isize t0 = get_ticks();

for (volatile long i = 0; i < 100000000; i++);  
isize t1 = get_ticks();
put_int(t1 - t0);
puts(" ticks for that busy loop\n");
return 0;
}