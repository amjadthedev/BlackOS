/* =============================================================================
 *  Black OS - freestanding string / memory primitives
 * ========================================================================== */
#include "string.h"

usize strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (usize)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, usize n)
{
    for (; n > 0; n--, a++, b++) {
        if (*a != *b)
            return (int)(unsigned char)*a - (int)(unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

char *strncpy(char *dst, const char *src, usize n)
{
    usize i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

void *memset(void *dst, int byte, usize len)
{
    u8 *d = (u8 *)dst;
    while (len--)
        *d++ = (u8)byte;
    return dst;
}

void *memcpy(void *dst, const void *src, usize len)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (len--)
        *d++ = *s++;
    return dst;
}

int memcmp(const void *a, const void *b, usize n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (; n > 0; n--, pa++, pb++)
        if (*pa != *pb)
            return (int)*pa - (int)*pb;
    return 0;
}

char *strstr(const char *haystack, const char *needle)
{
    if (needle[0] == '\0')
        return (char *)haystack;
    for (; *haystack; haystack++) {
        usize i = 0;
        while (needle[i] && haystack[i] == needle[i])
            i++;
        if (needle[i] == '\0')
            return (char *)haystack;
    }
    return (char *)0;
}

void *memmove(void *dst, const void *src, usize len)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d == s || len == 0)
        return dst;
    if (d < s) {
        while (len--)
            *d++ = *s++;
    } else {
        d += len;
        s += len;
        while (len--)
            *--d = *--s;
    }
    return dst;
}
