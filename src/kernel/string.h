/* =============================================================================
 *  Black OS - freestanding string / memory primitives
 * ========================================================================== */
#ifndef BLACKOS_STRING_H
#define BLACKOS_STRING_H

#include "types.h"

usize  strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, usize n);
char  *strncpy(char *dst, const char *src, usize n);
int    memcmp(const void *a, const void *b, usize n);
char  *strstr(const char *haystack, const char *needle);
void  *memset(void *dst, int byte, usize len);
void  *memcpy(void *dst, const void *src, usize len);
void  *memmove(void *dst, const void *src, usize len);

#endif /* BLACKOS_STRING_H */
