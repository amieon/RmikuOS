#include "schedlab.h"

int main(int argc, char **argv) {
    printf("There is nothing here: %d", getpid());
    // const char   *mode  = argc > 1 ? argv[1] : "adamw";
    // int          alpha0 = argc > 2 ? parse_int(argv[2]) : 50;
    // unsigned long total = argc > 3 ? (unsigned long)parse_int(argv[3]) : 36000;

    // sl_add_jobs("ctrl", 300, 8, /*period*/4, /*cpu*/2, /*burn*/400000);
    // sl_add_spin("ai",   100, 32, 12000);
    // sl_add_spin("log",   50,  4, 12000);
    // sl_add_spin_phased("dyn", 100, 16, 12000,
    //                    /*start*/12000, /*run*/12000);

    // sl_policy_t policy = sl_policy_adamw;
    // void       *ud     = &g_adamw;

    // if (str_eq(mode, "fixed")) {
    //     policy = 0;                     /* sl_run 内 alpha 冻结在 alpha0 */
    //     ud = 0;
    // } else if (str_eq(mode, "hill")) {
    //     sl_hill_init(&g_hill, alpha0, /*step*/5);
    //     policy = sl_policy_hill;
    //     ud = &g_hill;
    // } else {
    //     sl_adamw_init(&g_adamw, alpha0, /*lr*/8, /*target*/50);
    // }

    // printf("# mode=%s alpha0=%d total=%lu\n", mode, alpha0, total);

    // sl_run(&(sl_cfg){
    //     .total_ticks  = total,
    //     .window_ticks = 100,
    //     .alpha0       = alpha0,
    //     .policy       = policy,
    //     .policy_ud    = ud,
    // });
    // return 0;
}