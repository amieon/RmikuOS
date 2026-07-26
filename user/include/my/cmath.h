#include "../cmath.h"
#ifdef __cplusplus

namespace mymath {

inline double sin(double x)   { return mm_sin(x); }
inline double cos(double x)   { return mm_cos(x); }
inline double tan(double x)   { return mm_tan(x); }
inline void   sincos(double x, double *s, double *c) { mm_sincos(x, s, c); }

inline double asin(double x)  { return mm_asin(x); }
inline double acos(double x)  { return mm_acos(x); }
inline double atan(double x)  { return mm_atan(x); }
inline double atan2(double y, double x) { return mm_atan2(y, x); }

inline double sinh(double x)  { return mm_sinh(x); }
inline double cosh(double x)  { return mm_cosh(x); }
inline double tanh(double x)  { return mm_tanh(x); }

inline double exp(double x)   { return mm_exp(x); }
inline double log(double x)   { return mm_log(x); }
inline double log10(double x) { return mm_log10(x); }

inline double sqrt(double x)  { return mm_sqrt(x); }
inline double ceil(double x)  { return mm_ceil(x); }
inline double floor(double x) { return mm_floor(x); }
inline double fmod(double x, double y) { return mm_fmod(x, y); }
inline double modf(double x, double *iptr) { return mm_modf(x, iptr); }

/* float 重载 */
inline float sin(float x)     { return (float)mm_sin((double)x); }
inline float cos(float x)     { return (float)mm_cos((double)x); }
inline float tan(float x)     { return (float)mm_tan((double)x); }
inline void  sincos(float x, float *s, float *c) {
    double ds, dc; mm_sincos((double)x, &ds, &dc); *s = (float)ds; *c = (float)dc;
}
inline float asin(float x)    { return (float)mm_asin((double)x); }
inline float acos(float x)    { return (float)mm_acos((double)x); }
inline float atan(float x)    { return (float)mm_atan((double)x); }
inline float atan2(float y, float x) { return (float)mm_atan2((double)y, (double)x); }
inline float sinh(float x)    { return (float)mm_sinh((double)x); }
inline float cosh(float x)    { return (float)mm_cosh((double)x); }
inline float tanh(float x)    { return (float)mm_tanh((double)x); }
inline float exp(float x)     { return (float)mm_exp((double)x); }
inline float log(float x)     { return (float)mm_log((double)x); }
inline float log10(float x)   { return (float)mm_log10((double)x); }
inline float sqrt(float x)    { return (float)mm_sqrt((double)x); }
inline float ceil(float x)    { return (float)mm_ceil((double)x); }
inline float floor(float x)   { return (float)mm_floor((double)x); }
inline float fmod(float x, float y) { return (float)mm_fmod((double)x, (double)y); }
inline float modf(float x, float *iptr) {
    double di; float r = (float)mm_modf((double)x, &di); *iptr = (float)di; return r;
}

} /* namespace mymath */

#endif /* __cplusplus */
