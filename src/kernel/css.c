/* =============================================================================
*  Black OS - css.c : transparent selector rules (Phase 1, stubbed)
* ========================================================================== */
#include "css.h"
#include "string.h"
#include "printk.h"

static struct css_rule G_CSS_RULES[CSS_RULES_MAX];
static int G_CSS_COUNT = 0;

void css_init(void)
{
    memset(G_CSS_RULES, 0, sizeof(G_CSS_RULES));
    G_CSS_COUNT = 0;
}

int css_parse(const char *src, int len)
{
    if (!src) return -1;
    /* Phase 1: do not allocate parser; just log */
    (void)src;
    printk("[css] parse stub length=%d\n", len);
    return 0;
}

int css_rule_count(void) { return G_CSS_COUNT; }

const struct css_rule *css_rule_at(int i)
{
    if (i < 0 || i >= G_CSS_COUNT) return 0;
    return &G_CSS_RULES[i];
}

u32 css_match_color(const char *selector, u32 fallback)
{
    (void)selector;
    return fallback;
}
