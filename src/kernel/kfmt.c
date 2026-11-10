/* =============================================================================
 *  Black OS - kfmt.c: freestanding formatted string I/O
 *
 *  Backs every C library call the JavaScript engine (Duktape) links against:
 *
 *    vsnprintf / snprintf / sprintf / vsprintf   (+ __*_chk fortify wrappers)
 *    sscanf / __isoc99_sscanf                   (%d %i %u %x %X %o %p %c %s %n)
 *    __memcpy_chk / __memmove_chk / __memset_chk
 *    abort()          - longjmps to the JS recovery point when one is armed
 *    gettimeofday()   - uptime_ms based clock (Date/performance shims)
 *
 *  vsnprintf supports the full common language:
 *    flags   - + space # 0
 *    width   digits or *          (capped at 1000)
 *    prec    .digits or .*        (floats capped at 400)
 *    length  hh h l ll z L j t
 *    conv    c s d i u o x X p f F e E g G %
 *
 *  Floats are formatted from long double using top-down decimal digit
 *  generation (dec_norm: divide/multiply by 10^22 chunks) with rounding
 *  half-away guarded by an exact tail test (ties go half-to-even, matching
 *  the default FP rounding mode).  No u64->float casts, no libgcc.
 * ========================================================================== */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include "types.h"
#include "string.h"
#include "libm.h"
#include "printk.h"
#include "io.h"
#include "idt.h"
#include "rtc.h"

/* ------------------------------------------------------------- recover ---- */
/* Armed by ctx_js.c so abort() inside the engine unwinds to a safe point
 * instead of halting the machine. */
jmp_buf *k_recover_env = 0;

void abort(void)
{
    if (k_recover_env) {
        jmp_buf *e = k_recover_env;
        k_recover_env = 0;
        longjmp(*e, 1);
    }
    kprintf("[sys ] abort() - no recovery point, system halted\n");
    for (;;)
        hlt();
}

/* ----------------------------------------------------------- clock -------- */
/* Layout matches glibc's struct timeval on x86-64 (two longs). */
struct k_timeval {
    long tv_sec;
    long tv_usec;
};

/* Civil (y, m, d) -> days since 1970-01-01.  Howard Hinnant's public-domain
 * days_from_civil().  Signed integer division by constant works fine without
 * libgcc on x86-64 (idiv). */
static long days_from_civil(long y, long m, long d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5L + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static int clock_base_valid;
static u64 clock_base_sec;          /* RTC epoch seconds at anchor time    */
static u64 clock_anchor_ms;         /* uptime_ms at the same instant       */

int gettimeofday(struct k_timeval *tv, void *tz)
{
    (void)tz;

    if (!clock_base_valid) {
        struct rtc_time t;
        if (rtc_get(&t) == 0 && t.year >= 1970 && t.month >= 1 && t.month <= 12 &&
            t.day >= 1 && t.day <= 31) {
            u64 days = (u64)days_from_civil(t.year, t.month, t.day);
            u64 sec = days * 86400
                    + (u64)(unsigned)t.hour * 3600
                    + (u64)(unsigned)t.minute * 60
                    + (u64)(unsigned)t.sec;
            if (sec > 0) {
                clock_base_sec = sec;
                clock_anchor_ms = uptime_ms;
                clock_base_valid = 1;
            }
        }
        /* fall through to uptime-backed epoch if RTC could not be read     */
    }

    u64 delta = uptime_ms - clock_anchor_ms;
    u64 total_ms = (clock_base_valid ? clock_base_sec * 1000 : 0) + delta;

    if (tv) {
        tv->tv_sec = (long)(total_ms / 1000);
        tv->tv_usec = (long)((total_ms % 1000) * 1000);
    }
    return 0;
}

/* ---------------------------------------------------------- output sink --- */
struct ob {
    char  *buf;
    size_t cap;                 /* snprintf size; 0 writes nothing          */
    size_t len;                 /* would-be length (POSIX return value)     */
};

static void ob_put(struct ob *o, char c)
{
    if (o->cap && o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void ob_fill(struct ob *o, char c, int n)
{
    while (n-- > 0)
        ob_put(o, c);
}

static void ob_mem(struct ob *o, const char *s, int n)
{
    while (n-- > 0)
        ob_put(o, *s++);
}

/* ------------------------------------------------------------ spec -------- */
enum { F_LEFT = 1, F_ZERO = 2, F_PLUS = 4, F_SPACE = 8, F_ALT = 16 };
enum { LEN_NONE = 0, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_Z, LEN_LD, LEN_J };

struct spec {
    int  fl;
    int  width;                 /* 0 = none                                */
    int  prec;                  /* -1 = none                               */
    int  len;
    char cv;
};

static long long get_signed(va_list ap, int len)
{
    switch (len) {
    case LEN_HH: return (signed char)va_arg(ap, int);
    case LEN_H:  return (short)va_arg(ap, int);
    case LEN_L:  return va_arg(ap, long);
    case LEN_LL:
    case LEN_J:  return va_arg(ap, long long);
    case LEN_Z:  return (long long)va_arg(ap, long);
    default:     return va_arg(ap, int);
    }
}

static unsigned long long get_unsigned(va_list ap, int len)
{
    switch (len) {
    case LEN_HH: return (unsigned char)va_arg(ap, unsigned int);
    case LEN_H:  return (unsigned short)va_arg(ap, unsigned int);
    case LEN_L:  return va_arg(ap, unsigned long);
    case LEN_LL:
    case LEN_J:  return va_arg(ap, unsigned long long);
    case LEN_Z:  return va_arg(ap, unsigned long);
    default:     return va_arg(ap, unsigned int);
    }
}

/* Emit sign + prefix + body with width/alignment/zero padding. */
static void emit_num(struct ob *o, const char *body, int blen,
                     char sign, const char *prefix, int plen,
                     int width, int fl, int allow_zero)
{
    int total = blen + plen + (sign ? 1 : 0);
    int pad = (width > total) ? width - total : 0;
    int left = (fl & F_LEFT) != 0;
    int zero = allow_zero && !left && (fl & F_ZERO) != 0;

    if (!left && !zero)
        ob_fill(o, ' ', pad);
    if (sign)
        ob_put(o, sign);
    if (plen)
        ob_mem(o, prefix, plen);
    if (zero)
        ob_fill(o, '0', pad);
    ob_mem(o, body, blen);
    if (left)
        ob_fill(o, ' ', pad);
}

/* Reverse digit generation for integers (no libgcc divmod helpers needed). */
static int utoa_rev(char *t, unsigned long long v, int base, int upper)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0)
        t[n++] = '0';
    while (v) {
        t[n++] = dig[v % (unsigned)base];
        v /= (unsigned)base;
    }
    return n;                           /* reversed                            */
}

/* ------------------------------------------------------ float formatting -- */
typedef union {
    long double ld;
    struct { unsigned long long lo; unsigned short se; } s;
} ldbits;

static int ld_sign(long double x)
{
    ldbits u;
    u.ld = x;
    return (u.s.se >> 15) & 1;
}

static int is_inf_ld(long double x)
{
    ldbits u;
    u.ld = x;
    return (u.s.se & 0x7FFF) == 0x7FFF && u.s.lo == 0;
}

static int is_nan_ld(long double x)
{
    ldbits u;
    u.ld = x;
    return (u.s.se & 0x7FFF) == 0x7FFF && u.s.lo != 0;
}

/* v > 0 -> m in [1,10) with v ~ m * 10^e10.  Chunked by 10^22 (exact in
 * long double) so the operation count stays ~15+22 instead of 300+. */
static long double dec_norm(long double v, int *e10)
{
    int n = 0;
    while (v >= 1.0e22L) { v /= 1.0e22L; n += 22; }
    while (v >= 10.0L)    { v /= 10.0L;    n += 1;  }
    while (v != 0.0L && v * 1.0e22L < 1.0L) { v *= 1.0e22L; n -= 22; }
    while (v != 0.0L && v < 1.0L)           { v *= 10.0L;    n -= 1;  }
    *e10 = n;
    return v;
}

#define FMAXD 400                  /* integer digits generated (double: 309) */

/* Digits of |x| rounded at `prec` decimals.  Returns count n with the
 * decimal point after *ip characters (integer digit count, always >= 1).
 * The fractional part is exact (Sterbenz), so only the digit-by-digit
 * multiply-by-10 steps add ~2^-64 noise per digit. */
static int ffmt_digits(char *d, long double x, int prec, int *ip)
{
    long double ipart = floorl(x);
    long double fp = x - ipart;
    int n = 0, ii;

    if (ipart >= 1.0L) {
        int e10;
        long double m = dec_norm(ipart, &e10);
        ii = e10 + 1;
        if (ii > FMAXD)
            ii = FMAXD;
        for (int i = 0; i < ii; i++) {
            int dig = (int)m;
            if (dig > 9) dig = 9;
            if (dig < 0) dig = 0;
            d[n++] = (char)('0' + dig);
            m = (m - (long double)dig) * 10.0L;
        }
    } else {
        d[n++] = '0';
        ii = 1;
    }

    int guard = 0;
    long double tail = 0.0L;
    for (int i = 0; i <= prec; i++) {
        fp *= 10.0L;
        int dig = (int)fp;
        if (dig > 9) dig = 9;
        if (dig < 0) dig = 0;
        long double rem = fp - (long double)dig;
        if (i < prec) {
            d[n++] = (char)('0' + dig);
            fp = rem;
        } else {
            guard = dig;
            tail = rem;
        }
    }

    int up;
    if (guard > 5)      up = 1;
    else if (guard < 5) up = 0;
    else if (tail != 0.0L) up = 1;              /* > half: round up          */
    else                up = (d[n - 1] - '0') & 1;   /* tie: half to even    */

    if (up) {
        int k = n - 1;
        while (k >= 0 && d[k] == '9') {
            d[k] = '0';
            k--;
        }
        if (k >= 0) {
            d[k]++;
        } else {
            memmove(d + 1, d, (usize)n);        /* 999 -> 1000              */
            d[0] = '1';
            n++;
            ii++;
        }
    }
    *ip = ii;
    return n;
}

/* prec+1 rounded significant digits of |x| into d; *e10 with x ~ d.ddd*10^e. */
static int efmt_digits(char *d, long double x, int prec, int *e10)
{
    int e;
    long double m = dec_norm(x, &e);
    int n = 0, guard = 0;
    long double tail = 0.0L;

    for (int i = 0; i <= prec + 1; i++) {
        int dig = (int)m;
        if (dig > 9) dig = 9;
        if (dig < 0) dig = 0;
        long double rem = m - (long double)dig;
        if (i <= prec) {
            d[n++] = (char)('0' + dig);
            m = rem * 10.0L;
        } else {
            guard = dig;
            tail = rem;
            break;
        }
    }

    int up;
    if (guard > 5)      up = 1;
    else if (guard < 5) up = 0;
    else if (tail != 0.0L) up = 1;
    else                up = (d[n - 1] - '0') & 1;

    if (up) {
        int k = n - 1;
        while (k >= 0 && d[k] == '9') {
            d[k] = '0';
            k--;
        }
        if (k >= 0) {
            d[k]++;
        } else {
            d[0] = '1';                        /* 999 -> 100, exponent +1  */
            e++;
        }
    }
    *e10 = e;
    return n;
}

/* "e+05" / "E-12" (at least two exponent digits). */
static int emit_exp(char *b, int e, char ec)
{
    int k = 0;
    b[k++] = ec;
    b[k++] = e < 0 ? '-' : '+';
    int ae = e < 0 ? -e : e;
    char t[8];
    int m = 0;
    do {
        t[m++] = (char)('0' + ae % 10);
        ae /= 10;
    } while (ae);
    while (m < 2)
        t[m++] = '0';
    while (m > 0)
        b[k++] = t[--m];
    return k;
}

static void fmt_float(struct ob *o, long double x, const struct spec *sp,
                      char cv)
{
    int prec = sp->prec < 0 ? 6 : sp->prec;
    if (prec > 400)
        prec = 400;

    char sign = 0;
    if (ld_sign(x))
        sign = '-';
    else if (sp->fl & F_PLUS)
        sign = '+';
    else if (sp->fl & F_SPACE)
        sign = ' ';

    int upper = (cv >= 'F');                /* F E G                            */
    if (is_nan_ld(x)) {
        emit_num(o, upper ? "NAN" : "nan", 3, sign, 0, 0,
                 sp->width, sp->fl, 1);
        return;
    }
    if (is_inf_ld(x)) {
        emit_num(o, upper ? "INF" : "inf", 3, sign, 0, 0,
                 sp->width, sp->fl, 1);
        return;
    }

    long double ax = (sign == '-') ? -x : x;
    char low = (char)(cv | 0x20);
    char tmp[850];
    char body[900];
    int bl = 0;

    if (low == 'f') {
        int ip;
        int n = ffmt_digits(tmp, ax, prec, &ip);
        for (int i = 0; i < ip; i++)
            body[bl++] = tmp[i];
        if (prec > 0 || (sp->fl & F_ALT)) {
            body[bl++] = '.';
            for (int i = ip; i < n; i++)
                body[bl++] = tmp[i];
        }
    } else if (low == 'e') {
        int e;
        int n = efmt_digits(tmp, ax, prec, &e);
        body[bl++] = tmp[0];
        if (n > 1 || (sp->fl & F_ALT)) {
            body[bl++] = '.';
            for (int i = 1; i < n; i++)
                body[bl++] = tmp[i];
        }
        bl += emit_exp(body + bl, e, upper ? 'E' : 'e');
    } else {                                /* g                                */
        int e10;
        dec_norm(ax, &e10);
        int P = (prec == 0) ? 1 : prec;
        int alt = (sp->fl & F_ALT) != 0;
        int use_e = (ax != 0.0L) && (e10 < -4 || e10 >= P);

        if (!use_e) {
            int dp = P - 1 - e10;
            if (dp < 0)
                dp = 0;
            int ip;
            int n = ffmt_digits(tmp, ax, dp, &ip);
            if (!alt)
                while (n > ip && tmp[n - 1] == '0')
                    n--;
            for (int i = 0; i < ip; i++)
                body[bl++] = tmp[i];
            if (n > ip || alt) {
                body[bl++] = '.';
                for (int i = ip; i < n; i++)
                    body[bl++] = tmp[i];
            }
        } else {
            int e;
            int n = efmt_digits(tmp, ax, P - 1, &e);
            if (!alt)
                while (n > 1 && tmp[n - 1] == '0')
                    n--;
            body[bl++] = tmp[0];
            if (n > 1 || alt) {
                body[bl++] = '.';
                for (int i = 1; i < n; i++)
                    body[bl++] = tmp[i];
            }
            bl += emit_exp(body + bl, e, upper ? 'E' : 'e');
        }
    }

    emit_num(o, body, bl, sign, 0, 0, sp->width, sp->fl, 1);
}

/* ---------------------------------------------------------- vsnprintf ----- */
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct ob o;
    o.buf = buf;
    o.cap = n;
    o.len = 0;

    while (*fmt) {
        if (*fmt != '%') {
            ob_put(&o, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '\0')
            break;

        struct spec sp;
        sp.fl = 0;
        sp.width = 0;
        sp.prec = -1;
        sp.len = LEN_NONE;

        for (;;) {                          /* flags                            */
            if      (*fmt == '-') sp.fl |= F_LEFT;
            else if (*fmt == '0') sp.fl |= F_ZERO;
            else if (*fmt == '+') sp.fl |= F_PLUS;
            else if (*fmt == ' ') sp.fl |= F_SPACE;
            else if (*fmt == '#') sp.fl |= F_ALT;
            else break;
            fmt++;
        }
        if (*fmt == '*') {                  /* width                            */
            sp.width = va_arg(ap, int);
            fmt++;
            if (sp.width < 0) {
                sp.fl |= F_LEFT;
                sp.width = -sp.width;
            }
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                if (sp.width < 10000)
                    sp.width = sp.width * 10 + (*fmt - '0');
                fmt++;
            }
        }
        if (sp.width > 1000)
            sp.width = 1000;

        if (*fmt == '.') {                  /* precision                        */
            fmt++;
            if (*fmt == '*') {
                sp.prec = va_arg(ap, int);
                fmt++;
                if (sp.prec < 0)
                    sp.prec = -1;
            } else {
                sp.prec = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    if (sp.prec < 10000)
                        sp.prec = sp.prec * 10 + (*fmt - '0');
                    fmt++;
                }
                if (sp.prec > 400)
                    sp.prec = 400;
            }
        }

        if (*fmt == 'h') {                  /* length                           */
            fmt++;
            if (*fmt == 'h') { sp.len = LEN_HH; fmt++; } else sp.len = LEN_H;
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { sp.len = LEN_LL; fmt++; } else sp.len = LEN_L;
        } else if (*fmt == 'z') { sp.len = LEN_Z; fmt++; }
        else if (*fmt == 'j' || *fmt == 't') { sp.len = LEN_J; fmt++; }
        else if (*fmt == 'L') { sp.len = LEN_LD; fmt++; }

        sp.cv = *fmt;
        if (sp.cv == '\0')
            break;
        fmt++;

        switch (sp.cv) {
        case '%':
            ob_put(&o, '%');
            break;

        case 'c': {
            char ch = (char)va_arg(ap, int);
            int pad = sp.width > 1 ? sp.width - 1 : 0;
            if (!(sp.fl & F_LEFT))
                ob_fill(&o, ' ', pad);
            ob_put(&o, ch);
            if (sp.fl & F_LEFT)
                ob_fill(&o, ' ', pad);
            break;
        }

        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int sl = (int)strlen(s);
            if (sp.prec >= 0 && sp.prec < sl)
                sl = sp.prec;
            int pad = sp.width > sl ? sp.width - sl : 0;
            if (!(sp.fl & F_LEFT))
                ob_fill(&o, ' ', pad);
            ob_mem(&o, s, sl);
            if (sp.fl & F_LEFT)
                ob_fill(&o, ' ', pad);
            break;
        }

        case 'd':
        case 'i': {
            long long v = get_signed(ap, sp.len);
            unsigned long long mag =
                v < 0 ? (unsigned long long)(-(v + 1)) + 1
                      : (unsigned long long)v;
            char t[24];
            int n = utoa_rev(t, mag, 10, 0);
            int prec = sp.prec;
            if (prec > 64)
                prec = 64;
            if (prec == 0 && mag == 0)
                n = 0;
            char body[96];
            int bl = 0;
            int zeros = (prec > n) ? prec - n : 0;
            while (zeros-- > 0)
                body[bl++] = '0';
            while (n > 0)
                body[bl++] = t[--n];
            char sign = 0;
            if (v < 0)              sign = '-';
            else if (sp.fl & F_PLUS)  sign = '+';
            else if (sp.fl & F_SPACE) sign = ' ';
            emit_num(&o, body, bl, sign, 0, 0, sp.width, sp.fl,
                     sp.prec < 0);
            break;
        }

        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long v = get_unsigned(ap, sp.len);
            int base = (sp.cv == 'x' || sp.cv == 'X') ? 16
                     : (sp.cv == 'o') ? 8 : 10;
            char t[24];
            int n = utoa_rev(t, v, base, sp.cv == 'X');
            int prec = sp.prec;
            if (prec > 64)
                prec = 64;
            if (prec == 0 && v == 0)
                n = 0;
            char body[96];
            int bl = 0;
            int zeros = (prec > n) ? prec - n : 0;
            while (zeros-- > 0)
                body[bl++] = '0';
            while (n > 0)
                body[bl++] = t[--n];
            const char *prefix = 0;
            int plen = 0;
            if ((sp.fl & F_ALT) && v != 0) {
                if      (sp.cv == 'x') { prefix = "0x"; plen = 2; }
                else if (sp.cv == 'X') { prefix = "0X"; plen = 2; }
                else if (sp.cv == 'o' && body[0] != '0') {
                    prefix = "0";
                    plen = 1;
                }
            }
            emit_num(&o, body, bl, 0, prefix, plen, sp.width, sp.fl,
                     sp.prec < 0);
            break;
        }

        case 'p': {
            void *pv = va_arg(ap, void *);
            if (!pv) {
                int pad = sp.width > 5 ? sp.width - 5 : 0;
                if (!(sp.fl & F_LEFT))
                    ob_fill(&o, ' ', pad);
                ob_mem(&o, "(nil)", 5);
                if (sp.fl & F_LEFT)
                    ob_fill(&o, ' ', pad);
                break;
            }
            char t[24];
            int n = utoa_rev(t, (unsigned long long)(usize)pv, 16, 0);
            char body[32];
            int bl = 0;
            while (n > 0)
                body[bl++] = t[--n];
            emit_num(&o, body, bl, 0, "0x", 2, sp.width, sp.fl, 1);
            break;
        }

        case 'f': case 'F':
        case 'e': case 'E':
        case 'g': case 'G': {
            long double x = (sp.len == LEN_LD)
                                ? va_arg(ap, long double)
                                : (long double)va_arg(ap, double);
            fmt_float(&o, x, &sp, sp.cv);
            break;
        }

        default:                            /* unknown spec: emit as-is        */
            ob_put(&o, '%');
            ob_put(&o, sp.cv);
            break;
        }
    }

    if (n > 0)
        buf[o.len < n ? o.len : n - 1] = '\0';
    return (int)o.len;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

/* ------------------------------------------------------- fortify chk ------ */
int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen,
                    const char *fmt, va_list ap)
{
    (void)flag;
    if (maxlen > slen)
        abort();
    return vsnprintf(s, maxlen, fmt, ap);
}

int __snprintf_chk(char *s, size_t maxlen, int flag, size_t slen,
                   const char *fmt, ...)
{
    (void)flag;
    if (maxlen > slen)
        abort();
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, maxlen, fmt, ap);
    va_end(ap);
    return r;
}

int __sprintf_chk(char *s, int flag, size_t slen, const char *fmt, ...)
{
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, slen, fmt, ap);    /* slen == -1 -> unbounded        */
    va_end(ap);
    if (slen != (size_t)-1 && (size_t)r >= slen)
        abort();
    return r;
}

void *__memcpy_chk(void *dst, const void *src, size_t len, size_t dstlen)
{
    if (len > dstlen)
        abort();
    return memcpy(dst, src, (usize)len);
}

void *__memmove_chk(void *dst, const void *src, size_t len, size_t dstlen)
{
    if (len > dstlen)
        abort();
    return memmove(dst, src, (usize)len);
}

void *__memset_chk(void *dst, int c, size_t len, size_t dstlen)
{
    if (len > dstlen)
        abort();
    return memset(dst, c, (usize)len);
}

/* ------------------------------------------------------------- sscanf ----- */
static int s_ws(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

static int s_hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Optionally signed integer; base 0 = auto (0x/0 prefixes).  Advances *sp
 * only on success. */
static int s_int(const char **sp, int base, unsigned long long *out, int *neg)
{
    const char *s = *sp;
    while (s_ws(*s))
        s++;
    int sign = 0;
    if (*s == '+' || *s == '-') {
        sign = (*s == '-');
        s++;
    }
    int b = base;
    if (b == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && s_hexval(s[2]) >= 0)
            b = 16;
        else if (s[0] == '0' && s[1] >= '0' && s[1] <= '7')
            b = 8;
        else
            b = 10;
    }
    if (b == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        s_hexval(s[2]) >= 0)
        s += 2;
    unsigned long long v = 0;
    int any = 0;
    for (;;) {
        int d = s_hexval(*s);
        if (d < 0 || d >= b)
            break;
        v = v * (unsigned long long)b + (unsigned long long)d;
        s++;
        any = 1;
    }
    if (!any)
        return 0;
    *sp = s;
    *out = v;
    *neg = sign;
    return 1;
}

static int kvsscanf(const char *str, const char *fmt, va_list ap)
{
    const char *s = str;
    int assigned = 0;

    while (*fmt) {
        if (s_ws(*fmt)) {
            while (s_ws(*fmt))
                fmt++;
            while (s_ws(*s))
                s++;
            continue;
        }
        if (*fmt != '%') {
            if (*fmt != *s)
                return assigned;
            fmt++;
            s++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            if (*s != '%')
                return assigned;
            fmt++;
            s++;
            continue;
        }

        int suppress = 0, width = 0, len = LEN_NONE;
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            if (width < 10000)
                width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') { len = LEN_HH; fmt++; } else len = LEN_H;
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { len = LEN_LL; fmt++; } else len = LEN_L;
        } else if (*fmt == 'z') { len = LEN_Z; fmt++; }

        char cv = *fmt;
        if (cv == '\0')
            return assigned;
        fmt++;

        switch (cv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
        case 'p': {
            int base;
            if (cv == 'x' || cv == 'X' || cv == 'p')      base = 16;
            else if (cv == 'o')                           base = 8;
            else if (cv == 'i')                           base = 0;
            else                                          base = 10;
            unsigned long long v;
            int neg;
            if (!s_int(&s, base, &v, &neg))
                return assigned;
            if (!suppress) {
                if (cv == 'p') {
                    *va_arg(ap, void **) = (void *)(usize)v;
                } else {
                    long long sv = neg ? -(long long)v : (long long)v;
                    switch (len) {
                    case LEN_L:  *va_arg(ap, long *) = (long)sv; break;
                    case LEN_LL: *va_arg(ap, long long *) = sv; break;
                    case LEN_Z:  *va_arg(ap, unsigned long *) =
                                     neg ? (unsigned long)-(long long)v
                                         : (unsigned long)v; break;
                    case LEN_H:  *va_arg(ap, short *) = (short)sv; break;
                    case LEN_HH: *va_arg(ap, signed char *) =
                                     (signed char)sv; break;
                    default:     *va_arg(ap, int *) = (int)sv; break;
                    }
                }
                assigned++;
            }
            break;
        }

        case 'c': {
            int w = width > 0 ? width : 1;
            if ((usize)w > strlen(s))
                return assigned;
            if (!suppress) {
                char *dst = va_arg(ap, char *);
                for (int i = 0; i < w; i++)
                    dst[i] = s[i];
                assigned++;
            }
            s += w;
            break;
        }

        case 's': {
            while (s_ws(*s))
                s++;
            if (*s == '\0')
                return assigned;
            int n = 0;
            while (s[n] && !s_ws((unsigned char)s[n]) &&
                   (width == 0 || n < width))
                n++;
            if (!suppress) {
                char *dst = va_arg(ap, char *);
                for (int i = 0; i < n; i++)
                    dst[i] = s[i];
                dst[n] = '\0';
                assigned++;
            }
            s += n;
            break;
        }

        case 'n':
            if (!suppress)
                *va_arg(ap, int *) = (int)(s - str);
            break;

        default:
            return assigned;               /* unsupported conversion           */
        }
    }
    return assigned;
}

int vsscanf(const char *str, const char *fmt, va_list ap)
{
    return kvsscanf(str, fmt, ap);
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = kvsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int __isoc99_sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = kvsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}
