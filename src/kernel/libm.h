/* =============================================================================
 *  Black OS - libm.h : freestanding math surface (see libm.c)
 *
 *  Declares the long double cores used by kfmt.c (float formatting) and the
 *  double entry points Duktape links against.
 * ========================================================================== */
#ifndef BLACKOS_LIBM_H
#define BLACKOS_LIBM_H

/* double API (what Duktape references) */
double fabs(double x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double sqrt(double x);
double frexp(double x, int *exp);
double ldexp(double x, int n);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double pow(double x, double y);
double fmod(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double asin(double x);
double acos(double x);
double cbrt(double x);
double copysign(double m, double s);

/* long double API (kfmt.c + internal cores) */
long double fabsl(long double x);
long double floorl(long double x);
long double ceill(long double x);
long double truncl(long double x);
long double roundl(long double x);
long double sqrtl(long double x);
long double frexpl(long double x, int *exp);
long double ldexpl(long double x, int n);
long double expl(long double x);
long double logl(long double x);
long double log2l(long double x);
long double log10l(long double x);
long double powl(long double x, long double y);
long double fmodl(long double x, long double y);
long double sinl(long double x);
long double cosl(long double x);
long double tanl(long double x);
long double atanl(long double x);
long double atan2l(long double y, long double x);
long double asinl(long double x);
long double acosl(long double x);
long double cbrtl(long double x);
long double copysignl(long double mag, long double sgn);

#endif /* BLACKOS_LIBM_H */
