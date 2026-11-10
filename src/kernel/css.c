/* =============================================================================
 *  Black OS - css.c : CSS rule parser for the built-in browser
 *
 *  A minimal CSS cascade is all the browser needs:
 *
 *    <style> blocks are parsed into a flat rule table (selector x property
 *    -> value).  Values are kept as raw strings and decoded to u32 colors
 *    on demand (#rgb / #rrggbb / named CSS colors / rgb(r, g, b)).
 *
 *  Selector matching: comma/whitespace separated tokens, case-insensitive
 *  exact match on the element name (e.g. "body").
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

static char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int ci_starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (to_lower(*s) != to_lower(*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static char *trim(char *s)
{
    char *e;
    while (is_ws(*s))
        s++;
    e = s + strlen(s);
    while (e > s && is_ws(*(e - 1)))
        *--e = '\0';
    return s;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* --- named colors: a compact table of the most common CSS color names. ---- */
static const struct { const char *name; u32 rgb; } NAMED[] = {
    { "black",   0x000000 }, { "white",   0xFFFFFF }, { "red",   0xFF0000 },
    { "green",   0x008000 }, { "lime",    0x00FF00 }, { "blue",  0x0000FF },
    { "yellow",  0xFFFF00 }, { "orange",  0xFFA500 }, { "purple", 0x800080 },
    { "magenta", 0xFF00FF }, { "cyan",    0x00FFFF }, { "navy",   0x000080 },
    { "gray",    0x808080 }, { "grey",    0x808080 }, { "silver", 0xC0C0C0 },
    { "maroon",  0x800000 }, { "olive",   0x808000 }, { "teal",   0x008080 },
    { "aqua",    0x00FFFF }, { "fuchsia", 0xFF00FF }, { "brown",  0xA52A2A },
    { "pink",    0xFFC0CB }, { "gold",    0xFFD700 }, { "coral",  0xFF7F50 },
    { "crimson", 0xDC143C }, { "violet",  0xEE82EE }, { "indigo", 0x4B0082 },
    { "khaki",   0xF0E68C }, { "salmon",  0xFA8072 }, { "sienna", 0xA0522D },
    { "tan",     0xD2B48C },
};

/* Decode a CSS color value into 0x00RRGGBB.  Returns 1 on success. */
static int css_parse_color(const char *v, u32 *out)
{
    if (!v) return 0;

    /* rgb(r, g, b) - rgba(...) ignores the alpha channel */
    const char *s = v;
    while (is_ws(*s))
        s++;
    if (ci_starts_with(s, "rgba(") || ci_starts_with(s, "rgb(")) {
        const char *rgb = s + (to_lower(s[3]) == 'a' ? 5 : 4);
        u32 comps[3] = { 0, 0, 0 };
        int n = 0;
        while (*rgb && n < 3) {
            while (is_ws(*rgb) || *rgb == ',') rgb++;
            if (*rgb >= '0' && *rgb <= '9') {
                unsigned val = 0;
                while (*rgb >= '0' && *rgb <= '9') {
                    val = val * 10 + (unsigned)(*rgb - '0');
                    rgb++;
                }
                if (*rgb == '%') {              /* percent form */
                    val = (val * 255 + 50) / 100;
                    rgb++;
                }
                comps[n++] = val > 255 ? 255 : val;
            } else {
                break;
            }
        }
        if (n == 3) {
            *out = ((comps[0] << 16) | (comps[1] << 8) | comps[2]) & 0x00FFFFFF;
            return 1;
        }
        return 0;
    }

    if (*v == '#') {
        const char *h = v + 1;
        int n = 0;
        while (h[n] && !is_ws(h[n])) n++;
        if (n == 3 || n == 4) {
            int r = hexval(h[0]), g = hexval(h[1]), b = hexval(h[2]);
            if (r < 0 || g < 0 || b < 0) return 0;
            *out = (u32)(((r * 17) << 16) | ((g * 17) << 8) | (b * 17));
            return 1;
        }
        if (n >= 6) {
            int r = hexval(h[0]) * 16 + hexval(h[1]);
            int g = hexval(h[2]) * 16 + hexval(h[3]);
            int b = hexval(h[4]) * 16 + hexval(h[5]);
            if (r < 0 || g < 0 || b < 0) return 0;
            *out = (u32)((r << 16) | (g << 8) | b);
            return 1;
        }
        return 0;
    }

    /* named color */
    for (u32 i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); i++) {
        if (ci_eq(v, NAMED[i].name)) {
            *out = NAMED[i].rgb;
            return 1;
        }
    }
    return 0;
}

/* Split `selector` into comma/whitespace-separated element names and see if
 * `element` is one of them. */
static int selector_matches(const char *selector, const char *element)
{
    char cur[40];
    int n = 0;
    for (;;) {
        char c = *selector;
        if (c == '\0' || c == ',' || is_ws(c)) {
            if (n > 0) {
                cur[n] = '\0';
                if (ci_eq(cur, element))
                    return 1;
                n = 0;
            }
            if (c == '\0')
                return 0;
            selector++;
            continue;
        }
        if (n < (int)sizeof(cur) - 1)
            cur[n++] = c;
        selector++;
    }
}

/* Record one declaration (property:value) for the given selector into the
 * rule table. */
static void css_add_rule(const char *selector, const char *prop, const char *value)
{
    if (G_CSS_COUNT >= CSS_RULES_MAX)
        return;
    if (strlen(selector) >= sizeof(G_CSS_RULES[0].selector) ||
        strlen(prop) >= sizeof(G_CSS_RULES[0].property) ||
        strlen(value) >= sizeof(G_CSS_RULES[0].value))
        return;
    memset(&G_CSS_RULES[G_CSS_COUNT], 0, sizeof(G_CSS_RULES[0]));
    strcpy(G_CSS_RULES[G_CSS_COUNT].selector, selector);
    strcpy(G_CSS_RULES[G_CSS_COUNT].property, prop);
    strcpy(G_CSS_RULES[G_CSS_COUNT].value, value);
    G_CSS_COUNT++;
}

int css_parse(const char *src, int len)
{
    if (!src || len <= 0)
        return -1;

    const char *end = src + len;
    const char *p = src;
    int added = 0;

    while (p < end) {
        /* skip comments and whitespace */
        if (p[0] == '/' && p + 1 < end && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                p++;
            p = (p + 2 <= end) ? p + 2 : end;
            continue;
        }
        if (is_ws(*p)) {
            p++;
            continue;
        }

        /* ---- selector list ---------------------------------- */
        char sel[64];
        int sn = 0;
        while (p < end && *p != '{' && sn < (int)sizeof(sel) - 1) {
            sel[sn++] = *p++;
        }
        if (p >= end)
            break;
        sel[sn] = '\0';
        p++;                       /* skip '{' */

        /* ---- declaration block ------------------------------- */
        char block[256];
        int bn = 0;
        while (p < end && *p != '}' && bn < (int)sizeof(block) - 1) {
            block[bn++] = *p++;
        }
        if (p < end)
            p++;                   /* skip '}' */
        block[bn] = '\0';

        char *selector = trim(sel);
        if (!*selector)
            continue;

        /* Split declarations on ';' */
        char *blk = block;
        while (*blk) {
            char *semi = blk;
            while (*semi && *semi != ';')
                semi++;
            int dlen = (int)(semi - blk);

            char decl[64];
            if (dlen >= (int)sizeof(decl))
                dlen = (int)sizeof(decl) - 1;
            for (int i = 0; i < dlen; i++)
                decl[i] = blk[i];
            decl[dlen] = '\0';

            char *colon = decl;
            while (*colon && *colon != ':')
                colon++;
            if (*colon == ':') {
                *colon = '\0';
                char *prop = trim(decl);
                char *value = trim(colon + 1);
                if (*prop && *value) {
                    css_add_rule(selector, prop, value);
                    added++;
                }
            }
            blk = (*semi == ';') ? semi + 1 : semi;
        }
    }

    kprintf("[css] parsed %d rules (%d total)\n", added, G_CSS_COUNT);
    return added;
}

int css_rule_count(void) { return G_CSS_COUNT; }

const struct css_rule *css_rule_at(int i)
{
    if (i < 0 || i >= G_CSS_COUNT) return 0;
    return &G_CSS_RULES[i];
}

/* Resolve `element` ("body", "p", ...) `property` ignoring case. */
static const char *css_match_prop(const char *selector, const char *property)
{
    if (!selector)
        return 0;
    for (int i = 0; i < G_CSS_COUNT; i++) {
        if (selector_matches(G_CSS_RULES[i].selector, selector) &&
            ci_eq(G_CSS_RULES[i].property, property))
            return G_CSS_RULES[i].value;
    }
    return 0;
}

/* Public helpers ------------------------------------------------------------ */
u32 css_match_color_prop(const char *selector, const char *property, u32 fallback)
{
    const char *v = css_match_prop(selector, property);
    u32 rgb;
    if (v && css_parse_color(v, &rgb))
        return rgb;
    return fallback;
}

u32 css_match_color(const char *selector, u32 fallback)
{
    return css_match_color_prop(selector, "color", fallback);
}
