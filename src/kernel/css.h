/* =============================================================================
*  Black OS - css.h : tiny selector-rules parser (Phase 1 scaffold)
* ========================================================================== */
#ifndef BLACKOS_CSS_H
#define BLACKOS_CSS_H

#include "types.h"

#define CSS_RULES_MAX 64

struct css_rule {
    char selector[32];
    char property[32];
    char value[64];
};

void css_init(void);
int  css_parse(const char *src, int len);int  css_rule_count(void);
const struct css_rule *css_rule_at(int i);
u32  css_match_color(const char *selector, u32 fallback);

#endif /* BLACKOS_CSS_H */
