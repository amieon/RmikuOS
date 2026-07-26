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
int isspace(int c){return _ctab[(unsigned char)c]&1;}
int isdigit(int c){return _ctab[(unsigned char)c]&2;}
int isxdigit(int c){unsigned char u=c;return (u>='0'&&u<='9')||(u>='a'&&u<='f')||(u>='A'&&u<='F');}
int isalpha(int c){return _ctab[(unsigned char)c]&4;}
int isalnum(int c){unsigned char u=c;return _ctab[u]&6;}
int iscntrl(int c){unsigned char u=c;return u<32||u==127;}
int toupper(int c){unsigned char u=c;return (u>='a'&&u<='z')?u-32:u;}
int tolower(int c){unsigned char u=c;return (u>='A'&&u<='Z')?u+32:u;}

#endif