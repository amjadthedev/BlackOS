/* =============================================================================
 *  Black OS - filetype.h : MIME / extension registry for known file types
 *
 *  Points each extension (.pkg, .py, .html, .exe, ...) to a friendly icon
 *  color and (optionally) a launcher hint so Explorer icons + browser
 *  downloads can render a consistent launcher.
 * ========================================================================== */
#ifndef BLACKOS_FILETYPE_H
#define BLACKOS_FILETYPE_H

#include "types.h"

#define FT_UNKNOWN   0
#define FT_PACKaged  1   /* .pkg / package */
#define FT_Executbl  2   /* .exe / .bin / ELF */
#define FT_CodePy    3   /* .py */
#define FT_CodeCFam  4   /* .c / .h / .cpp */
#define FT_Html      5   /* .html / .htm */
#define FT_Css       6   /* .css */
#define FT_Json      7   /* .json */
#define FT_Text      8   /* .txt */
#define FT_ImagePng  9   /* .png */
#define FT_ImageJpg  10  /* .jpg / .jpeg */
#define FT_Zip      11   /* .zip */
#define FT_DocPdf   12   /* .pdf */
#define FT_Other   99

struct file_type_desc {
    u32  type;
    u32  icon_color;
    u32  size_hint;
    char ext[8];
    char name[32];
};

void ft_init(void);
u32  ft_get_type(const char *path);
const struct file_type_desc *ft_get_desc(u32 type);
u32  ft_get_assoc_color(const char *path);

#endif /* BLACKOS_FILETYPE_H */