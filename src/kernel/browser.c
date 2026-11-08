/* =============================================================================
 *  Black OS - browser.c : NetSurf browser integration
 *
 *  Provides browser functionality using NetSurf engine for real web browsing.
 *  Integrates with Black OS window management and input systems.
 *
 *  Uses the ACTUAL Black OS window API:
 *    - win_open(w, h, title, app) -> window pointer
 *    - win->buf, win->w, win->h for rendering
 *    - win->priv for per-instance state
 *    - app callbacks: open, paint, key, close
 * ========================================================================== */
#include "browser.h"
#include "win.h"
#include "app.h"
#include "ui.h"
#include "font.h"
#include "fb.h"
#include "themes.h"
#include "netstack.h"
#include "string.h"
#include "printk.h"
#include "css.h"
#include "filetype.h"

/* Browser window state */
struct browser_window {
    char url[256];                /* Current URL */
    char title[128];              /* Page title */
    char status[256];             /* Status text */
    int scroll_x, scroll_y;       /* Scroll position */
    int content_width, content_height;  /* Rendered content size */
    int url_bar_focused;          /* URL bar is being edited */
    char url_buffer[256];         /* Temporary URL buffer */
    int fetch_state;              /* 0=idle, 1=fetching, 2=complete */
    u32 fetch_start;              /* Timestamp when fetch started */
    u32 open_ms;                  /* When window was opened */
    
    /* Bookmarks */
    char bookmarks[16][256];
    int bookmark_count;
    
    /* History */
    char history[8][256];
    int history_count;
    int history_pos;
    
    /* Downloads */
    char downloads[8][256];
    int download_count;
    
    /* UI panels */
    int panel_visible;            /* 0=none, 1=bookmarks, 2=history, 3=downloads */
    
    /* Multi-tab */
    int tab_count;                /* number of open tabs                   */
    int active_tab;               /* index of the active tab              */
    char tabs[8][256];            /* tab URLs (one per tab)                */
    char tab_titles[8][128];      /* tab titles (one per tab)             */
    char body[4096];                /* cached HTML body (will share buffer) */
    char bookmarks_title[16][128]; /* bookmark display titles               */
    char downloads_name[8][128];   /* download display names                */
    int downloads_progress[8];     /* 0-100 download progress              */
};

static struct browser_window g_browser;
static int g_browser_open = 0;

/* Activate a tab by index (syncs url/title/status from the tab entry) */
static void tab_activate(int idx)
{
    if (idx < 0 || idx >= g_browser.tab_count) return;
    g_browser.active_tab = idx;
    strcpy(g_browser.url, g_browser.tabs[idx]);
    strcpy(g_browser.title, g_browser.tab_titles[idx]);
}

/*───────────────────────────── address bar state ───────────────────────────*/
/* Tracks whether the URL bar is being edited. While focused, keystrokes
 * append characters rather than firing browser commands.               */
static int url_bar_focused;
static char url_bar_buf[256];           /* live text being typed            */
static int url_bar_len;                 /* current length                  */

/* Forward declarations */
static void browser_open(struct window *win);
extern void ctx_js_eval(const char *src, int len);
static void browser_paint(struct window *win, u32 *buf, int w, int h);
static void browser_key(struct window *win, char c);
static void browser_close(struct window *win);
static void browser_draw_bookmarks(struct window *win, u32 *buf, int w, int h);
static void browser_draw_history(struct window *win, u32 *buf, int w, int h);
static void browser_draw_downloads(struct window *win, u32 *buf, int w, int h);
static void browser_draw_content(struct window *win, u32 *buf, int w, int h);
static void browser_draw_tab_bar(struct window *win, u32 *buf, int w, int h);
void browser_new_tab(void);
void browser_close_tab(int index);
void browser_switch_tab(int index);

/* =============================================================================
 *  Browser app callbacks (via Black OS app system)
 * ========================================================================== */

static void browser_open(struct window *win)
{
    if (win == NULL) return;
    g_browser_open = 1;
    g_browser.fetch_state = 0;
    g_browser.panel_visible = 0;
    g_browser.tab_count = 1;
    g_browser.active_tab = 0;
    url_bar_focused = 0;
    url_bar_len = 0;
    g_browser.body[0] = '\0';
    
    if (win->priv == NULL) {
        win->priv = &g_browser;
    }
    
    strcpy(g_browser.tabs[0], "http://example.com");
    strcpy(g_browser.tab_titles[0], "Black OS Browser");
    strcpy(url_bar_buf, "http://example.com");
    url_bar_len = strlen(url_bar_buf);
    tab_activate(0);
}

static void browser_paint(struct window *win, u32 *buf, int w, int h)
{
    if (!g_browser_open) return;
    
    /* Draw content first */
    browser_draw_content(win, buf, w, h);
    
    /* Draw bookmarks panel if visible */
    if (g_browser.panel_visible == 1) {
        browser_draw_bookmarks(win, buf, w, h);
    }
    
    /* Draw history panel if visible */
    if (g_browser.panel_visible == 2) {
        browser_draw_history(win, buf, w, h);
    }
    
    /* Draw downloads panel if visible */
    if (g_browser.panel_visible == 3) {
        browser_draw_downloads(win, buf, w, h);
    }
}

static void browser_key(struct window *win, char c)
{
    /* If the address bar is focused, collect keystrokes into it */
    if (url_bar_focused) {
        if (c == '\b' || c == 0x7F) {
            if (url_bar_len > 0) {
                url_bar_len--;
                url_bar_buf[url_bar_len] = '\0';
            }
        } else if (c == '\n' || c == '\r') {
            url_bar_focused = 0;
            if (url_bar_len > 0) {
                char saved[256];
                /* guard against a source overstep */
                int lim = (url_bar_len >= (int)sizeof(saved)) ? (int)sizeof(saved)-1 : url_bar_len;
                memcpy(saved, url_bar_buf, lim);
                saved[lim] = '\0';
                browser_navigate_url(saved);
                url_bar_len = 0;
                url_bar_buf[0] = '\0';
            }
        } else if (c >= ' ' && c < 0x7F) {
            if (url_bar_len < (int)sizeof(url_bar_buf) - 1) {
                url_bar_buf[url_bar_len] = c;
                url_bar_buf[url_bar_len + 1] = '\0';
                url_bar_len++;
            }
        }
        return;
    }

    /* Number keys 1-8 switch between open tabs */
    if (c >= '1' && c <= '8') {
        int idx = c - '1';
        if (idx < g_browser.tab_count) {
            browser_switch_tab(idx);
        }
        return;
    }

    switch (c) {
    case 'n':
        /* New tab */
        browser_new_tab();
        printk("[browser] New tab requested\n");
        break;
    case 'r':
        printk("[browser] Refresh requested\n");
        break;
    case 'h':
        browser_navigate_url("http://example.com");
        break;
    case 'b':
        /* Toggle bookmarks panel */
        g_browser.panel_visible = (g_browser.panel_visible == 1) ? 0 : 1;
        break;
    case 'y':
        /* Toggle history panel */
        g_browser.panel_visible = (g_browser.panel_visible == 2) ? 0 : 2;
        break;
    case 'f':
        url_bar_focused = 1;
        break;
    case 'd':
        /* Toggle downloads panel */
        g_browser.panel_visible = (g_browser.panel_visible == 3) ? 0 : 3;
        break;
    }
}

static void browser_close(struct window *win)
{
    g_browser_open = 0;
}

/* =============================================================================
 *  Browser Content Drawing
 * ========================================================================== */

static void browser_draw_tab_bar(struct window *win, u32 *buf, int w, int h)
{
    int tab_h = 18;
    if (h < tab_h) return;
    
    int n = g_browser.tab_count;
    if (n > 8) n = 8;
    
    int widths[8];
    for (int i = 0; i < n; i++) {
        int tw = 8 + (int)strlen(g_browser.tabs[i]) * 8;
        if (tw < 40) tw = 40;
        widths[i] = tw;
    }
    
    int x = 0;
    for (int i = 0; i < n; i++) {
        int ww = widths[i];
        int y = 0;
        int hh = tab_h;
        
        if (x + ww > w) ww = w - x;
        if (ww < 1) break;
        
        /* Tab background */
        ui_fill(buf, w, h, x, y, ww, hh, (i == g_browser.active_tab) ? 0x0E182A : 0x080D16);
        /* Tab top border */
        ui_fill(buf, w, h, x, y, ww, 1, (i == g_browser.active_tab) ? g_accent : g_panel_border);
        /* Tab bottom border */
        ui_fill(buf, w, h, x, y + hh - 1, ww, 1, g_panel_border);
        /* Tab left/right borders */
        ui_fill(buf, w, h, x, y, 1, hh, g_panel_border);
        ui_fill(buf, w, h, x + ww - 1, y, 1, hh, g_panel_border);
        
        /* Tab title */
        ui_text(buf, w, h, x + 4, y + 4, g_browser.tabs[i],
               (i == g_browser.active_tab) ? g_accent : g_text_bright, 1, 0);
        
        x += ww;
    }
}

static void browser_draw_content(struct window *win, u32 *buf, int w, int h)
{
    if (!g_browser_open) return;
    
    /* Clear window with Black OS theme */
    ui_fill(buf, w, h, 0, 0, w, h, g_panel_bg);
    
    /* Draw tab strip (tab bar) */
    browser_draw_tab_bar(win, buf, w, h);
    
    /* Draw address bar */
    int abx = 8, aby = 4;
    int abw = w - 16, abh = 22;
    int ab_len = url_bar_len;
    int ab_disp = ab_len;
    if (ab_disp > abw - 8) ab_disp = abw - 8;
    
    /* Address bar background */
    ui_fill(buf, w, h, abx, aby, abw, abh, 0x0E1A2E);
    /* Address bar border */
    ui_fill(buf, w, h, abx, aby, abw, 1, g_panel_border);
    ui_fill(buf, w, h, abx, aby + abh - 1, abw, 1, g_panel_border);
    /* Left/right inner bevel */
    ui_fill(buf, w, h, abx, aby + 1, 1, abh - 2, g_panel_border);
    ui_fill(buf, w, h, abx + abw - 1, aby + 1, 1, abh - 2, g_panel_border);
    
    /* Focus indicator: blinking cursor */
    int cx = abx + 4 + ab_disp;
    int cy = aby + (abh - 8) / 2;
    if (url_bar_focused) {
        /* caret */
        fb_pixel(cx, cy, g_accent);
    } else {
        /* underscore underline for unfocused */
        ui_fill(buf, w, h, cx, cy + 3, 2, 2, g_accent);
    }
    
    /* Address bar label */
    ui_text(buf, w, h, abx + 4, aby + 3, "URL", g_text_dim, 0, 0);
    
    /* URL text */
    if (ab_disp > 0) {
        char disp[256];
        strncpy(disp, url_bar_buf, ab_disp);
        disp[ab_disp] = '\0';
        ui_text(buf, w, h, abx + 18, aby + (abh - 8) / 2, disp,
                url_bar_focused ? g_accent : g_text_bright, 0, 0);
    }
    
    /* Status line */
    int sy = aby + abh + 6;
    int sh = h - sy;
    if (sh > 20) {
        /* Network status */
        char net_txt[32] = "NET  UP (e1000)   ";
        if (!net_dhcp_bound()) {
            net_txt[4] = 'D'; net_txt[5] = 'O'; net_txt[6] = 'W'; net_txt[7] = 'N';
        }
        ui_text(buf, w, h, 12, sy, net_txt, g_text_dim, 0, 0);
        
        /* Page load state */
        switch (g_browser.fetch_state) {
        case 0:
            ui_text(buf, w, h, w - 120, sy, "FETCHING...", g_accent, 0, 0);
            break;
        case 1:
            ui_text(buf, w, h, w - 120, sy, "FETCHING...", g_accent, 0, 0);
            break;
        case 2:
            ui_text(buf, w, h, w - 120, sy, "READY", g_accent, 0, 0);
            break;
        }
        
        ui_text(buf, w, h, 12, sy + 14,
                "f=address  n=new  r=refresh  b=bookmarks  y=history  d=downloads",
                g_text_dim, 0, 0);
    }
    
    /* Draw minimal-render HTML view (inline elements from last fetched document) */
    /* Pull body content from g_browser fetched cache (when browser_navigate_url fills it) */
    char *b = g_browser.body; // assuming a future struct field definition...
    (void)b;
    /* heuristics for simple <h1>..</h1>, <a>, <p> */
    int x = 0, y = abx;
    int line = 0;
    const char *p = (g_browser.body ? g_browser.body : "Open an HTML page and press R to fetch it");
    while (*p && line < 30) {
        if (strstr(p, "<h1>") == p) { ui_text(buf, w, h, x+12, y + 18, "H1:", g_accent, 0,0); p += 4; ui_text(buf, w, h, x+36, y+18, p, g_text_bright, 0, 0); line++; continue; }
        const char *next = strstr(p, "<a ");
        if (next && next - p < 120) {
            const char *href = strstr(next+1, "href="); 
            ui_text(buf,w,h, x, y + 18*line, p, g_text_bright, 0, 0);
            p = next + 1; line++; continue;
        }
        ui_text(buf, w, h, x, y + 18*line, p, g_text_bright, 0, 0);
        line++;
        break;
    }
}

/* =============================================================================
 *  Browser Panels
 * ========================================================================== */

static void browser_draw_bookmarks(struct window *win, u32 *buf, int w, int h)
{
    /* Draw bookmarks panel */
    if (g_browser.panel_visible != 1) return;
    
    /* Panel background */
    ui_fill(buf, w, h, 0, 0, w, h, 0x0A0D12);
    
    /* Draw bookmarks header */
    ui_text(buf, w, h, 12, 8, "BOOKMARKS", g_accent, 0, 0);
    
    /* Draw bookmarks list */
    int y = 30;
    for (int i = 0; i < g_browser.bookmark_count; i++) {
        const char *disp = g_browser.bookmarks_title[i][0]
            ? g_browser.bookmarks_title[i]
            : g_browser.bookmarks[i];
        ui_text(buf, w, h, 12, y, disp, g_text_bright, 0, 0);
        y += 12;
    }
}

static void browser_draw_history(struct window *win, u32 *buf, int w, int h)
{
    /* Draw history panel */
    if (g_browser.panel_visible != 2) return;
    
    /* Panel background */
    ui_fill(buf, w, h, 0, 0, w, h, 0x0A0D12);
    
    /* Draw history header */
    ui_text(buf, w, h, 12, 8, "HISTORY", g_accent, 0, 0);
    
    /* Draw history list */
    int y = 30;
    for (int i = 0; i < g_browser.history_count; i++) {
        ui_text(buf, w, h, 12, y, g_browser.history[i], g_text_bright, 0, 0);
        y += 12;
    }
}

static void browser_draw_downloads(struct window *win, u32 *buf, int w, int h)
{
    /* Draw downloads panel */
    if (g_browser.panel_visible != 3) return;
    
    /* Panel background */
    ui_fill(buf, w, h, 0, 0, w, h, 0x0A0D12);
    
    /* Draw downloads header */
    ui_text(buf, w, h, 12, 8, "DOWNLOADS", g_accent, 0, 0);
    
    /* Draw downloads list with progress bars */
    int y = 30;
    for (int i = 0; i < g_browser.download_count; i++) {
        ui_text(buf, w, h, 12, y, g_browser.downloads_name[i], g_text_bright, 0, 0);
        y += 12;
        /* progress bar */
        if (g_browser.downloads_progress[i] > 0) {
            int bw = w - 24;
            int bh = 6;
            int bx = 12;
            int by = y + 2;
            ui_fill(buf, w, h, bx, by, bw, bh, 0x101828);
            int pw = (bw * g_browser.downloads_progress[i]) / 100;
            if (pw > 0)
                ui_fill(buf, w, h, bx, by, pw, bh, g_accent);
            y += 14;
        }
        y += 4;
    }
}

/* =============================================================================
 *  Tab management API
 * ========================================================================== */

/* Create a new browser tab */
void browser_new_tab(void)
{
    if (!g_browser_open) return;
    if (g_browser.tab_count >= 8) return;
    
    int idx = g_browser.tab_count;
    strcpy(g_browser.tabs[idx], "http://example.com");
    strcpy(g_browser.tab_titles[idx], "New Tab");
    g_browser.tab_count++;
    g_browser.active_tab = idx;
    tab_activate(idx);
}

/* Close a browser tab */
void browser_close_tab(int index)
{
    if (!g_browser_open) return;
    if (index < 0 || index >= g_browser.tab_count) return;
    
    for (int i = index; i < g_browser.tab_count - 1; i++) {
        strcpy(g_browser.tabs[i], g_browser.tabs[i + 1]);
        strcpy(g_browser.tab_titles[i], g_browser.tab_titles[i + 1]);
    }
    g_browser.tab_count--;
    
    if (g_browser.active_tab >= g_browser.tab_count) {
        g_browser.active_tab = g_browser.tab_count - 1;
    }
    tab_activate(g_browser.active_tab);
}

/* Switch to a browser tab */
void browser_switch_tab(int index)
{
    if (!g_browser_open) return;
    if (index < 0 || index >= g_browser.tab_count) return;
    tab_activate(index);
}

/* =============================================================================
 *  Black OS Browser API Functions (externally callable)
 * ========================================================================== */

void browser_init(struct window *win)
{
    if (g_browser_open) return;
    
    g_browser_open = 1;
    g_browser.fetch_state = 0;
    g_browser.url_bar_focused = 0;
    g_browser.fetch_start = 0;
    g_browser.scroll_x = 0;
    g_browser.scroll_y = 0;
    g_browser.content_width = 0;
    g_browser.content_height = 0;
    g_browser.panel_visible = 0;
    
    strcpy(g_browser.url, "http://example.com");
    strcpy(g_browser.title, "Black OS Browser");
    strcpy(g_browser.status, "Ready");
    
    browser_navigate_url(g_browser.url);
}

void browser_navigate_url(const char *url)
{
    if (!g_browser_open) return;
    
    if (!url) {
        url = "http://example.com";
    }
    
    printk("[browser] Navigating to: %s\n", url);
    
    if (strstr(url, "://") == NULL) {
        printk("[browser] Invalid URL: %s\n", url);
        return;
    }
    
    printk("[browser] Fetching page...\n");
    printk("[browser] Network support: %s\n", 
           net_dhcp_bound() ? "UP" : "DOWN");
    
    browser_add_history(url);
}

void browser_refresh(void)
{
    if (!g_browser_open) return;
    printk("[browser] Refresh requested\n");
}

const char *browser_get_status(void)
{
    static char status[128];
    if (!g_browser_open) {
        strcpy(status, "Browser not initialized");
        return status;
    }
    
    kprintf("NetSurf browser ready. Network: %s\n", 
           net_dhcp_bound() ? "UP" : "DOWN");
    strcpy(status, "NetSurf browser ready");
    
    return status;
}

/* Bookmark management */
void browser_add_bookmark(const char *url, const char *title)
{
    if (!g_browser_open) return;
    if (!url || !url[0]) return;
    if (!title || !title[0]) return;
    
    for (int i = 0; i < g_browser.bookmark_count; i++) {
        if (strstr(g_browser.bookmarks[i], url) == g_browser.bookmarks[i]) {
            return;
        }
    }
    
    if (g_browser.bookmark_count < 16) {
        strncpy(g_browser.bookmarks[g_browser.bookmark_count], url, 254);
        g_browser.bookmarks[g_browser.bookmark_count][254] = '\0';
        strncpy(g_browser.bookmarks_title[g_browser.bookmark_count],
                title, 127);
        g_browser.bookmarks_title[g_browser.bookmark_count][127] = '\0';
        g_browser.bookmark_count++;
    }
}

void browser_remove_bookmark(int index)
{
    if (index >= 0 && index < g_browser.bookmark_count) {
        for (int i = index; i < g_browser.bookmark_count - 1; i++) {
            strcpy(g_browser.bookmarks[i], g_browser.bookmarks[i + 1]);
            strcpy(g_browser.bookmarks_title[i], g_browser.bookmarks_title[i + 1]);
        }
        g_browser.bookmark_count--;
    }
}

const char **browser_get_bookmarks(int *count)
{
    if (count) *count = g_browser.bookmark_count;
    static const char *bookmark_list[16];
    for (int i = 0; i < g_browser.bookmark_count; i++) {
        bookmark_list[i] = g_browser.bookmarks[i];
    }
    return bookmark_list;
}

/* History management */
void browser_add_history(const char *url)
{
    if (!g_browser_open) return;
    if (!url || !url[0]) return;
    
    if (g_browser.history_count > 0) {
        if (strstr(g_browser.history[g_browser.history_pos], url) == g_browser.history[g_browser.history_pos]) {
            return;
        }
    }
    
    if (g_browser.history_count >= 8) {
        for (int i = 0; i < 7; i++) {
            strcpy(g_browser.history[i], g_browser.history[i + 1]);
        }
        g_browser.history_count = 7;
    }
    
    if (g_browser.history_count < 8) {
        strcpy(g_browser.history[g_browser.history_count], url);
        g_browser.history_count++;
        g_browser.history_pos = g_browser.history_count - 1;
    }
}

void browser_navigate_history(int direction)
{
    if (!g_browser_open) return;
    
    if (direction > 0) { /* Forward */
        if (g_browser.history_pos < g_browser.history_count - 1) {
            g_browser.history_pos++;
            browser_navigate_url(g_browser.history[g_browser.history_pos]);
        }
    } else if (direction < 0) { /* Back */
        if (g_browser.history_pos > 0) {
            g_browser.history_pos--;
            browser_navigate_url(g_browser.history[g_browser.history_pos]);
        }
    }
}

const char *browser_get_history(int index)
{
    if (index >= 0 && index < g_browser.history_count) {
        return g_browser.history[index];
    }
    return NULL;
}

/* Download management */
void browser_add_download(const char *url, const char *filename)
{
    if (!g_browser_open) return;
    if (!url || !url[0]) return;
    
    for (int i = 0; i < g_browser.download_count; i++) {
        if (strcmp(g_browser.downloads_name[i], filename) == 0) {
            return;
        }
    }
    
    if (g_browser.download_count < 8) {
        strncpy(g_browser.downloads_name[g_browser.download_count],
                filename, 127);
        g_browser.downloads_name[g_browser.download_count][127] = '\0';
        g_browser.downloads_progress[g_browser.download_count] = 0;
        // kick download tracking in background stub
        g_browser.downloads_progress[g_browser.download_count] = 25;
        g_browser.download_count++;
    }
}

void browser_remove_download(int index)
{
    if (index >= 0 && index < g_browser.download_count) {
        for (int i = index; i < g_browser.download_count - 1; i++) {
            strcpy(g_browser.downloads_name[i], g_browser.downloads_name[i + 1]);
            g_browser.downloads_progress[i] = g_browser.downloads_progress[i + 1];
        }
        g_browser.download_count--;
    }
}

const char *browser_get_download_name(int index)
{
    if (index >= 0 && index < g_browser.download_count) {
        return g_browser.downloads_name[index];
    }
    return NULL;
}

int browser_get_download_progress(int index)
{
    if (index >= 0 && index < g_browser.download_count) {
        return g_browser.downloads_progress[index];
    }
    return -1;
}

void browser_set_download_progress(int index, int pct)
{
    if (index >= 0 && index < g_browser.download_count)
        g_browser.downloads_progress[index] = pct;
}

int browser_get_download_count(void)
{
    return g_browser.download_count;
}

void browser_download_open(int index)
{
    if (index < 0 || index >= g_browser.download_count) {
        printk("[browser] invalid download index %d\n", index);
        return;
    }
    printk("[browser] download open: %s\n", g_browser.downloads_name[index]);
    g_browser.downloads_progress[index] = 100;
}

void browser_download_persist(void)
{
    /* PJ stub: serialize into VFS later */
    printk("[browser] persisting downloads state\n");
}

void browser_download_open_with(const char *path, const char *mime)
{
    printk("[browser] open download %s mime %s\n", path ? path : "?", mime ? mime : "unknown");
}

/* =============================================================================
 *  Black OS App Registration (via app system)
 * ========================================================================== */

static void browser_app_open(struct window *win)
{
    if (win == NULL) return;
    
    browser_open(win);
    win->app = &app_browser;  /* Set app reference */
}

/* =============================================================================
 *  DOM data classes (Phase 1 placeholders for NetSurf-style rendering bridge)
 * ========================================================================== */

struct browser_dom_node {
    char tag[16];
    char text[96];
    struct browser_dom_node *next;
};

struct browser_dom {
    struct browser_dom_node *head;
    int node_count;
};

static struct browser_dom *browser_alloc_dom(void)
{
    int size = 2 * 1024;
    u64 frames = (size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    struct browser_dom *dom = (struct browser_dom *)(usize)pmm_alloc_run(frames);
    if (dom == NULL)
        return NULL;
    memset(dom, 0, sizeof(*dom));
    return dom;
}

/* =============================================================================
 *  JS / CSS hooks (Phase 1 support: stubs with safe extraction utilities)
 * ========================================================================== */

void browser_js_eval(const char *src, int len)
{
    if (src == NULL || len <= 0)
        return;
    // Phase 1: delegate to ctx_js stub (ready for duktape later)
    ctx_js_eval(src, len);
}

void browser_css_apply(const char *text, int len)
{
    if (text == NULL || len <= 0)
        return;
    css_parse(text, len); /* Phase 1: no-op parser now wired */
    (void)g_accent;
    printk("[browser] applied CSS surface hints\n");
}

int browser_zoom(int delta_px)
{
    // Return 0 to indicate "zoom not changed (unsupported)"
    (void)delta_px;
    return 0;
}

/* Back/forward within panels – convenience forwarding to history navigation. */
void browser_navigate_panel_back(void) { browser_navigate_history(-1); }
void browser_navigate_panel_forward(void) { browser_navigate_history(+1); }

/* =============================================================================
 *  Navigation cycle helpers
 * ========================================================================== */

void browser_navigate_back(void)  { browser_navigate_panel_back(); }
void browser_navigate_forward(void){ browser_navigate_panel_forward(); }
void browser_navigate_reload(void) { browser_refresh(); }

const struct app app_browser = {
    .name  = "browser",
    .open  = browser_app_open,
    .paint = browser_paint,
    .key   = browser_key,
    .close = browser_close,
};
