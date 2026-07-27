/* locale.h —— C 标准区域设置（stub，RmikuOS 无多语言支持） */
#ifndef LOCALE_H
#define LOCALE_H

struct lconv {
    char *decimal_point;    /* "." */
    char *thousands_sep;    /* "" */
    char *grouping;         /* "" */
    char *int_curr_symbol;  /* "" */
    char *currency_symbol;  /* "" */
    char *mon_decimal_point;/* "" */
    char *mon_thousands_sep;/* "" */
    char *mon_grouping;     /* "" */
    char *positive_sign;    /* "" */
    char *negative_sign;    /* "-" */
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
};

/* 全局唯一实例，所有字段都是 C/POSIX 默认值 */
static struct lconv __rmiku_lconv = {
    ".", "", "", "", "", "", "", "", "", "-",
    127, 127, 127, 127, 127, 127, 127, 127
};

static inline struct lconv *localeconv(void) {
    return &__rmiku_lconv;
}

/* 设置 locale，RmikuOS 只支持 "C" */
static inline char *setlocale(int category, const char *locale) {
    (void)category;
    if (!locale) return "C";
    if (locale[0] == 'C' && locale[1] == '\0') return "C";
    return 0;  /* 不支持其他 locale */
}

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

#endif