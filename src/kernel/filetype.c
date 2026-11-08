/* =============================================================================
 *  Black OS - filetype.c : extension->MIME-type registry
 * ========================================================================== */
#include "filetype.h"
#include "string.h"
#include "printk.h"
#include <stdint.h>
#include "themes.h"
int strnicmp(const char *a, const char *b, usize n);

static struct file_type_desc table[] = {
    { FT_PACKaged,  0x00C0E8, 4096,  "pkg",  ".pkg"  },
    { FT_Executbl,  0xD42222, 4096,  "exe",  ".exe"  },
    { FT_CodePy,    0xFFC800, 2048,  "py",   ".py"   },
    { FT_CodeCFam,  0x4E9DD8, 4096,  "c",    ".c"    },
    { FT_Html,       0xE88F40, 2048,  "html", ".html" },
    { FT_Css,        0xC068FF, 2048,  "css",  ".css"  },
    { FT_Json,       0xFFD966, 2048,  "json", ".json" },
    { FT_Text,       0x7089A6, 2048,  "txt",  ".txt"  },
    { FT_ImagePng,   0x00C8C8, 32768, "png",  ".png"  },
    { FT_ImageJpg,   0x00CCAA, 32768, "jpg",  ".jpg"  },
    { FT_Zip,        0x996633, 8192,  "zip",  ".zip"  },
    { FT_DocPdf,     0xD43333, 8192,  "pdf",  ".pdf"  },
    { FT_Other,      0x7089A6, 2048,  "?",    ""      },
};

void ft_init(void)
{
    int n = (int)(sizeof(table) / sizeof(table[0]));
    printk("[ft] %d file type descriptors loaded\n", n);
    (void)n;
}

static const char *filetype_tail(const char *path) {
    if (path == NULL) return "";
    const char *tail = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') tail = p + 1;
    }
    return tail;
}

u32 ft_get_type(const char *path)
{
    const char *t = filetype_tail(path);
    if (!t || !t[0]) return FT_UNKNOWN;
    int len = (int)strlen(t);
    if (len == 0) return FT_UNKNOWN;
    /* match suffix */
    for (int i = 0; table[i].type != FT_Other; i++) {
        const char *ext = table[i].ext;
        int elen = strlen(ext);
        if (elen && len >= elen && strnicmp(t + len - elen, ext, elen) == 0) {
            return table[i].type;
        }
    }
    return FT_UNKNOWN;
}

/* no strnicmp in our helper impl — provide a case-insensitive suffix check */
int strnicmp(const char *a, const char *b, usize n)
{
    for (usize i = 0; i < n; i++) {
        // case folding
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

const struct file_type_desc *ft_get_desc(u32 type)
{
    for (int i = 0; table[i].type != FT_Other; i++) {
        if (table[i].type == type) return &table[i];
    }
    for (int i = 0; table[i].type != FT_Other; i++) {
        if (table[i].type == FT_Other) return &table[i];
    }
    return NULL;
}

u32 ft_get_assoc_color(const char *path)
{
    u32 t = ft_get_type(path);
    const struct file_type_desc *d = ft_get_desc(t);
    return d ? d->icon_color : g_text_dim;
}