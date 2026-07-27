#pragma once
#ifndef CTYPE
#define CTYPE

static const unsigned char _ctab[256] = {
    0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,  /* 0-15: \t\n\v\f\r are space */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 32=' ' space */
    2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,  /* 48-57='0'-'9' digit */
    0,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,  /* 65-90='A'-'Z' alpha */
    4,4,4,4,4,4,4,4,4,4,4,0,0,0,0,0,
    0,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,  /* 97-122='a'-'z' alpha */
    4,4,4,4,4,4,4,4,4,4,4,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static inline int isspace(int c){return _ctab[(unsigned char)c]&1;}
static inline int isdigit(int c){return _ctab[(unsigned char)c]&2;}
static inline int isxdigit(int c){unsigned char u=c;return (u>='0'&&u<='9')||(u>='a'&&u<='f')||(u>='A'&&u<='F');}
static inline int isalpha(int c){return _ctab[(unsigned char)c]&4;}
static inline int isalnum(int c){unsigned char u=c;return _ctab[u]&6;}
static inline int iscntrl(int c){unsigned char u=c;return u<32||u==127;}
static inline int toupper(int c){unsigned char u=c;return (u>='a'&&u<='z')?u-32:u;}
static inline int tolower(int c){unsigned char u=c;return (u>='A'&&u<='Z')?u+32:u;}
static inline int isupper(int c){unsigned char u=c;return u>='A'&&u<='Z';}
static inline int islower(int c){unsigned char u=c;return u>='a'&&u<='z';}
static inline int isgraph(int c){unsigned char u=c;return u>32&&u<127;}
static inline int ispunct(int c){unsigned char u=c;return u>32&&u<127&&!(_ctab[u]&6);}
static inline int isprint(int c){unsigned char u=c;return u>=32&&u<127;}

#endif