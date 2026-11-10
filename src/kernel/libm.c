/* =============================================================================
 *  Black OS - freestanding libm backing the JavaScript engine (Duktape)
 *
 *  The kernel links no libc, so every math symbol Duktape references is
 *  implemented here from scratch.  All cores run in long double (x87 80-bit,
 *  ~19-20 significant digits) so the double wrappers round correctly: the
 *  working error stays near 1e-19, well under half an ulp of a double.
 *
 *  Techniques:
 *    exp   - reduce x = k*ln2 + f with a two-word ln2 (exact), Taylor in f
 *    log   - frexpl + atanh series; near 1.0 a direct log1p series avoids
 *            the cancellation between e*ln2 and the series
 *    sin/c - reduce mod pi/2 with a two-word constant, quadrant switch,
 *            Taylor on |r| <= pi/4
 *    atan  - two algebraic reductions then a Taylor series
 *    pow   - integer part by binary exponentiation, fraction via exp/log
 *    cbrt  - Newton iteration from a frexpl-derived exponent guess
 *
 *  No libgcc helpers: long double <-> integer conversions are done by hand
 *  (floor via significand masking) because the kernel links without libgcc.
 * ========================================================================== */
#include "libm.h"

/* ------------------------------------------------------------- constants -- */
#define LD_PI       3.1415926535897932384626433832795028842L
#define LD_PI_2     1.5707963267948966192313216916397514421L
#define LD_PI_2_HI  0xc90fdaa22168c234p-63L    /* top 64 bits of pi/2      */
#define LD_PI_2_LO  0xc4c6628b80dc1cd1p-127L   /* next 64; tail < 2^-131    */
#define LD_2_PI     0.63661977236758134307553505349005744814L
#define LD_LN2      0.69314718055994530941723212145817656808L
#define LD_LOG2E    1.4426950408889634073599246810018921374L
#define LD_LOG10E   0.43429448190325182765112891891660508229L

#define LD_EXP_MAX  11356.5L            /* ln(max long double) ~ 11355.13   */

static const long double LD_HALF_MAX = 9223372036854775808.0L;  /* 2^63     */

/* --------------------------------------------------------------- helpers -- */
typedef union {
    long double ld;
    struct { unsigned long long lo; unsigned short se; } s;
} ld_bits;

/* Split a constant so k*hi and k*lo are both exactly representable for
 * |k| < 2^32: hi keeps the top 32 significand bits, lo holds the rest. */
static long double ld_split(long double v, long double *lo)
{
    ld_bits u;
    u.ld = v;
    u.s.lo &= 0xFFFFFFFF00000000ULL;
    *lo = v - u.ld;
    return u.ld;
}

/* 2^n by binary squaring (n clamped to the long double range). */
static long double pow2l(int n)
{
    if (n > 16384)  return __builtin_infl();
    if (n < -16445) return 0.0L;
    long double r = 1.0L, b = 2.0L;
    unsigned m = (unsigned)(n < 0 ? -n : n);
    while (m) {
        if (m & 1) r *= b;
        b *= b;
        m >>= 1;
    }
    return n < 0 ? 1.0L / r : r;
}

/* floor(|x|) for x in [0, 2^64) - no float->int conversion, no libgcc. */
static unsigned long long ld_floor_ull(long double x)
{
    if (!(x >= 1.0L))
        return 0;
    ld_bits u;
    int e;
    u.ld = frexpl(x, &e);            /* value = sig * 2^(e-64), sig 64-bit  */
    int sh = e - 64;
    if (sh >= 64)
        return 0;                    /* multiple of 2^64: low bits are 0     */
    if (sh >= 0)
        return u.s.lo << sh;         /* shift in [0,63] - no UB             */
    return u.s.lo >> (-sh);          /* e >= 1 for x >= 1, so -sh <= 63      */
}

/* floor(x) as a long long for |x| < 2^63 (clamped beyond). */
static long long ld_to_ll(long double x)
{
    if (x != x)
        return 0;
    if (x >= LD_HALF_MAX)
        return 9223372036854775807LL;
    if (x <= -LD_HALF_MAX)
        return -9223372036854775807LL - 1;
    long double f = floorl(x);
    if (f >= 0)
        return (long long)ld_floor_ull(f);
    return -(long long)ld_floor_ull(-f);
}

/* ------------------------------------------------------------ basic ops -- */
long double fabsl(long double x) { return x < 0 ? -x : x; }
double fabs(double x) { return (double)fabsl((long double)x); }

long double copysignl(long double mag, long double sgn)
{
    ld_bits a, b;
    a.ld = mag;
    b.ld = sgn;
    a.s.se = (unsigned short)((a.s.se & 0x7FFF) | (b.s.se & 0x8000));
    return a.ld;
}
double copysign(double m, double s)
{
    return (double)copysignl((long double)m, (long double)s);
}

/* floor by masking the fractional significand bits (exact, no conversions). */
long double floorl(long double x)
{
    ld_bits u;
    if (x != x || x == 0)
        return x;
    int neg = x < 0;
    u.ld = neg ? -x : x;
    if ((u.s.se & 0x7FFF) == 0x7FFF)
        return x;                       /* +/- inf                          */
    int expf = (int)(u.s.se & 0x7FFF) - 16383;
    if (expf >= 63)
        return x;                       /* integral (and anything huge)     */
    if (expf < 0)
        return neg ? -1.0L : 0.0L;      /* 0 < |x| < 1                      */
    u.s.lo &= ~((1ULL << (63 - expf)) - 1);
    if (neg) {
        long double t = u.ld;           /* floor(|x|)                       */
        if (t != -x)
            t += 1.0L;                  /* floor(x) = -ceil(|x|)            */
        return -t;
    }
    return u.ld;
}
double floor(double x) { return (double)floorl((long double)x); }

long double ceill(long double x)
{
    ld_bits u;
    if (x != x || x == 0)
        return x;
    int neg = x < 0;
    u.ld = neg ? -x : x;
    if ((u.s.se & 0x7FFF) == 0x7FFF)
        return x;
    int expf = (int)(u.s.se & 0x7FFF) - 16383;
    if (expf >= 63)
        return x;
    if (expf < 0)
        return neg ? -0.0L : 1.0L;      /* 0 < |x| < 1                      */
    u.s.lo &= ~((1ULL << (63 - expf)) - 1);
    if (neg)
        return -u.ld;                   /* ceil(-a) = -floor(a)             */
    long double t = u.ld;
    if (t != x)
        t += 1.0L;
    return t;
}
double ceil(double x) { return (double)ceill((long double)x); }

long double truncl(long double x) { return x < 0 ? ceill(x) : floorl(x); }
double trunc(double x) { return (double)truncl((long double)x); }

/* Round half away from zero. */
long double roundl(long double x)
{
    if (x != x)
        return x;
    if (x < 0)
        return -roundl(-x);
    long double t = floorl(x);
    if (x - t >= 0.5L)
        t += 1.0L;
    return t;
}
double round(double x) { return (double)roundl((long double)x); }

long double sqrtl(long double x)
{
    if (x != x)
        return x;
    if (x < 0)
        return __builtin_nanl("");
    if (x == 0)
        return x;
    return __builtin_sqrtl(x);           /* x87 fsqrt: exact, no libcall    */
}
double sqrt(double x) { return (double)sqrtl((long double)x); }

/* ------------------------------------------------------- frexp / ldexp ---- */
long double frexpl(long double x, int *exp)
{
    ld_bits u;
    if (x != x || x == 0) {
        *exp = 0;
        return x;
    }
    u.ld = x;
    if ((u.s.se & 0x7FFF) == 0x7FFF) {   /* inf / nan                        */
        *exp = 0;
        return x;
    }
    int e = (int)(u.s.se & 0x7FFF) - 16383;
    u.s.se = (unsigned short)((u.s.se & 0x8000) | 16382);  /* bias to -1     */
    *exp = e + 1;
    return u.ld;
}
double frexp(double x, int *e) { return (double)frexpl((long double)x, e); }

long double ldexpl(long double x, int n)
{
    long double r = x;
    if (x == 0 || x != x)
        return x;
    while (n > 16000) { r *= pow2l(16000); n -= 16000; if (r == 0) break; }
    while (n < -16000) { r *= pow2l(-16000); n += 16000; if (r == 0) break; }
    return r * pow2l(n);
}
double ldexp(double x, int n) { return (double)ldexpl((long double)x, n); }

/* ---------------------------------------------------------------- exp ----- */
long double expl(long double x)
{
    if (x != x)
        return x;
    if (x > LD_EXP_MAX)
        return __builtin_infl();
    if (x < -LD_EXP_MAX)
        return 0.0L;

    long double ln2lo;
    long double ln2hi = ld_split(LD_LN2, &ln2lo);
    long double kf = floorl(x * LD_LOG2E);
    int k = (int)ld_to_ll(kf);
    long double f = (x - kf * ln2hi) - kf * ln2lo;   /* |f| < ln2           */

    long double sum = 1.0L, term = 1.0L;
    for (int n = 1; n <= 32; n++) {
        term *= f / (long double)n;
        sum += term;
        if (term == 0)
            break;
    }
    return sum * pow2l(k);
}
double exp(double x) { return (double)expl((long double)x); }

/* ---------------------------------------------------------------- log ----- */
/* Direct log(1+y) series for x near 1: no e*ln2 cancellation, so the error
 * scales with the (small) result rather than with ln(2). */
static long double log_near_one(long double x)
{
    long double y = x - 1.0L;            /* exact: x in (0.5,1.5)           */
    long double term = y, sum = y;
    long double y2 = y * y;
    for (int n = 2; n <= 80; n++) {
        term *= -y;
        sum += term / (long double)n;
        if (term == 0)
            break;
    }
    (void)y2;
    return sum;
}

long double logl(long double x)
{
    if (x != x)
        return x;
    if (x < 0)
        return __builtin_nanl("");
    if (x == 0)
        return -__builtin_infl();
    if (x == __builtin_infl())
        return x;
    if (x > 0.5L && x < 1.5L)
        return log_near_one(x);

    int e;
    long double m = frexpl(x, &e);       /* m in [0.5,1)                    */
    long double t = (m - 1.0L) / (m + 1.0L);          /* |t| <= 1/3         */
    long double t2 = t * t;
    long double term = t, sum = t;
    for (int n = 3; n <= 61; n += 2) {
        term *= t2;
        sum += term / (long double)n;
    }
    return (long double)e * LD_LN2 + 2.0L * sum;
}
double log(double x) { return (double)logl((long double)x); }

long double log2l(long double x)
{
    if (x != x)
        return x;
    if (x <= 0 || x == __builtin_infl())
        return logl(x);
    /* Near 1 the result is tiny while e and log2(m) are O(1): that sum
     * cancels catastrophically.  Compute log1p(x-1)/ln2 directly there
     * (x-1 is exact on [0.5, 2] by Sterbenz). */
    if (x > 0.5L && x < 1.5L)
        return log_near_one(x) * LD_LOG2E;
    int e;
    long double m = frexpl(x, &e);
    if (m == 0.5L)
        return (long double)e - 1.0L;    /* exact for powers of two: x = m*2^e */
    return (long double)e + logl(m) * LD_LOG2E;
}
double log2(double x) { return (double)log2l((long double)x); }

long double log10l(long double x) { return logl(x) * LD_LOG10E; }
double log10(double x) { return (double)log10l((long double)x); }

/* ---------------------------------------------------------------- pow ----- */
long double powl(long double x, long double y)
{
    if (y == 0)
        return 1.0L;                     /* incl. 0^0 and NaN^0             */
    if (x != x)
        return x;
    if (y != y)
        return y;

    int neg = x < 0;
    long double ax = neg ? -x : x;

    if (ax == 0)                         /* 0^positive = 0, 0^-positive = inf */
        return y > 0 ? 0.0L : __builtin_infl();
    if (ax == __builtin_infl())
        return y > 0 ? (neg && fabsl(y) == 1 ? -__builtin_infl()
                                              : __builtin_infl())
                      : 0.0L;
    if (y == __builtin_infl())
        return ax < 1 ? 0.0L : __builtin_infl();
    if (y == -__builtin_infl())
        return ax < 1 ? __builtin_infl() : 0.0L;

    long double fy = floorl(y);          /* integer part (toward -inf)      */
    long double fr = y - fy;             /* fraction in [0,1)               */
    if (neg && fr != 0)
        return __builtin_nanl("");       /* negative base needs an integer  */

    long double r = 1.0L;

    /* Integer part: binary exponentiation when it fits in 64 bits. */
    if (fy >= -9.0e18L && fy <= 9.0e18L) {
        long long n = ld_to_ll(fy);
        long long e = n < 0 ? -n : n;
        long double b = neg ? -ax : ax;
        while (e) {
            if (e & 1)
                r *= b;
            b *= b;
            e >>= 1;
        }
        if (n < 0)
            r = 1.0L / r;
    } else {
        /* |y| > 9e18: parity is unknowable but such values are even anyway
         * (their ulp is >= 2), so a negative base yields a positive result. */
        r = expl(fy * logl(ax));
    }

    if (fr != 0)
        r *= expl(fr * logl(ax));
    return r;
}
double pow(double x, double y)
{
    return (double)powl((long double)x, (long double)y);
}

/* ---------------------------------------------------------------- fmod ---- */
long double fmodl(long double x, long double y)
{
    if (x != x || y != y)
        return __builtin_nanl("");
    if (y == 0)
        return __builtin_nanl("");       /* C99: fmod(x, +-0) is NaN         */
    if (x == 0)
        return x;
    if (x == __builtin_infl())
        return __builtin_nanl("");
    if (y == __builtin_infl())
        return x;

    long double ay = fabsl(y);
    long double r = x;
    int e = 0;
    while (r >= ay || r <= -ay) {        /* exact: scaling by powers of 2    */
        r *= 0.5L;
        e++;
    }
    while (e > 0) {                      /* scale back, re-reducing each step */
        r *= 2.0L;
        if (r >= ay)
            r -= ay;
        else if (r <= -ay)
            r += ay;
        e--;
    }
    return r;
}
double fmod(double x, double y)
{
    return (double)fmodl((long double)x, (long double)y);
}

/* ------------------------------------------------------------ trig -------- */
static long double sin_core(long double r)
{
    long double r2 = r * r, term = r, sum = r;
    for (int n = 1; n <= 24; n++) {
        term *= -r2 / (long double)((2 * n) * (2 * n + 1));
        sum += term;
        if (term == 0)
            break;
    }
    return sum;
}

static long double cos_core(long double r)
{
    long double r2 = r * r, term = 1.0L, sum = 1.0L;
    for (int n = 1; n <= 24; n++) {
        term *= -r2 / (long double)((2 * n - 1) * (2 * n));
        sum += term;
        if (term == 0)
            break;
    }
    return sum;
}

/* --- two-word (double-double) arithmetic for argument reduction ---------- */
/* Veltkamp split for p = 64: c = 2^(p/2+1) + 1. */
static void split64(long double a, long double *hi, long double *lo)
{
    long double c = 8589934593.0L;              /* 2^33 + 1              */
    long double t = c * a;
    *hi = t - (t - a);
    *lo = a - *hi;
}

/* hi + lo = a*b, both words correct (Dekker two-product). */
static void two_prod(long double a, long double b, long double *hi, long double *lo)
{
    *hi = a * b;
    long double ah, al, bh, bl;
    split64(a, &ah, &al);
    split64(b, &bh, &bl);
    *lo = ((ah * bh - *hi) + ah * bl + al * bh) + al * bl;
}

/* s + e = a + b exactly (Knuth two-sum). */
static void two_sum(long double a, long double b, long double *s, long double *e)
{
    *s = a + b;
    long double v = *s - a;
    *e = (a - (*s - v)) + (b - v);
}

/* Reduce x mod pi/2: return r with x = kr*(pi/2) + r, |r| <= pi/4, and the
 * quadrant kr mod 4.  A single-word product kr*(pi/2) leaves an error of
 * ~ulp(x), which dominates results near zero (sin(pi) would be pure noise),
 * so the product is formed in two words from a 128-bit pi/2 constant.
 * Remaining error: omitted pi/2 tail (~2^-131)*kr plus a few ulps of r. */
static long double reduce_pi2(long double x, int *q)
{
    long double kr = roundl(x * LD_2_PI);
    int neg = kr < 0;
    unsigned long long mag = ld_floor_ull(neg ? -kr : kr);
    int qi = (int)(mag & 3u);
    *q = neg ? ((4 - qi) & 3) : qi;             /* two's-complement mod 4  */

    long double ph, pl, ql, ql2;
    two_prod(kr, LD_PI_2_HI, &ph, &pl);
    two_prod(kr, LD_PI_2_LO, &ql, &ql2);

    long double s, e;
    two_sum(x, -ph, &s, &e);
    return (((s + e) - pl) - ql) - ql2;
}

long double sinl(long double x)
{
    int q;
    long double r = reduce_pi2(x, &q);
    long double s = sin_core(r), c = cos_core(r);
    switch (q) {
    case 0:  return s;
    case 1:  return c;
    case 2:  return -s;
    default: return -c;
    }
}
double sin(double x) { return (double)sinl((long double)x); }

long double cosl(long double x)
{
    int q;
    long double r = reduce_pi2(x, &q);
    long double s = sin_core(r), c = cos_core(r);
    switch (q) {
    case 0:  return c;
    case 1:  return -s;
    case 2:  return -c;
    default: return s;
    }
}
double cos(double x) { return (double)cosl((long double)x); }

long double tanl(long double x)
{
    int q;
    long double r = reduce_pi2(x, &q);
    long double s = sin_core(r), c = cos_core(r);
    return (q & 1) ? -c / s : s / c;
}
double tan(double x) { return (double)tanl((long double)x); }

/* --------------------------------------------------------------- atan ----- */
/* Two applications of atan(a) = 2*atan(a/(1+sqrt(1+a^2))): the argument
 * drops from <= 1 to <= 0.199, so 24 Taylor terms overshoot 64-bit
 * precision.  Each application contributes a factor 2 (hence 4 below). */
static long double atan_core(long double a)     /* a in [0,1]               */
{
    long double t = a / (1.0L + sqrtl(1.0L + a * a));
    t = t / (1.0L + sqrtl(1.0L + t * t));       /* |t| <= 0.199             */
    long double t2 = t * t;
    long double term = 1.0L, sum = 1.0L;
    for (int k = 1; k <= 24; k++) {
        term *= -t2;
        sum += term / (long double)(2 * k + 1);
        if (term == 0)
            break;
    }
    return 4.0L * t * sum;
}

long double atanl(long double x)
{
    if (x != x)
        return x;
    if (x == __builtin_infl())
        return LD_PI_2;
    if (x == -__builtin_infl())
        return -LD_PI_2;
    int neg = x < 0;
    long double a = neg ? -x : x;
    int inv = 0;
    if (a > 1.0L) {
        a = 1.0L / a;
        inv = 1;
    }
    long double r = atan_core(a);
    if (inv)
        r = LD_PI_2 - r;
    return neg ? -r : r;
}
double atan(double x) { return (double)atanl((long double)x); }

long double atan2l(long double y, long double x)
{
    if (x != x || y != y)
        return __builtin_nanl("");
    if (x == 0 && y == 0)
        return copysignl(0.0L, y);
    if (x == 0)
        return y > 0 ? LD_PI_2 : -LD_PI_2;
    if (y == 0)
        return x > 0 ? 0.0L : LD_PI;
    long double r = atanl(y / x);
    if (x > 0)
        return r;
    return y > 0 ? r + LD_PI : r - LD_PI;
}
double atan2(double y, double x)
{
    return (double)atan2l((long double)y, (long double)x);
}

long double asinl(long double x)
{
    if (x != x)
        return x;
    if (x > 1.0L || x < -1.0L)
        return __builtin_nanl("");
    if (x == 1.0L)
        return LD_PI_2;
    if (x == -1.0L)
        return -LD_PI_2;
    return atanl(x / sqrtl((1.0L - x) * (1.0L + x)));
}
double asin(double x) { return (double)asinl((long double)x); }

long double acosl(long double x)
{
    if (x != x)
        return x;
    if (x > 1.0L || x < -1.0L)
        return __builtin_nanl("");
    return atan2l(sqrtl((1.0L - x) * (1.0L + x)), x);
}
double acos(double x) { return (double)acosl((long double)x); }

/* --------------------------------------------------------------- cbrt ----- */
long double cbrtl(long double x)
{
    if (x != x || x == 0)
        return x;
    int neg = x < 0;
    long double a = neg ? -x : x;
    if (a == __builtin_infl())
        return neg ? -a : a;

    int e;
    (void)frexpl(a, &e);
    int e3 = e / 3;
    if (e3 * 3 > e)
        e3--;                            /* floor division for negatives     */
    long double r = pow2l(e3);
    for (int i = 0; i < 64; i++) {       /* Newton: r = (2r + a/r^2)/3       */
        long double nr = (2.0L * r + a / (r * r)) / 3.0L;
        if (nr == r)
            break;
        r = nr;
    }
    return neg ? -r : r;
}
double cbrt(double x) { return (double)cbrtl((long double)x); }
