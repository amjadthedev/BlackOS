/* =============================================================================
 *  Black OS - browser.h : NetSurf browser API wrapper
 *
 *  Provides browser functionality for Black OS applications.
 *  Integrates NetSurf-like web browsing with Black OS window management.
 * ========================================================================== */
#ifndef BLACKOS_BROWSER_H
#define BLACKOS_BROWSER_H

#include "types.h"
#include "win.h"
#include "app.h"

/* Browser API functions */
void browser_init(struct window *win);
void browser_navigate_url(const char *url);
void browser_refresh(void);
const char *browser_get_status(void);

/* JavaScript / CSS support (Phase 1: stub hooks) */
void browser_js_eval(const char *src, int len);
void browser_css_apply(const char *text, int len);
int  browser_zoom(int delta_px);
void browser_navigate_panel_back(void);
void browser_navigate_panel_forward(void);

/* Tab management */
void browser_new_tab(void);
void browser_close_tab(int index);
void browser_switch_tab(int index);

/* Bookmark management */
void browser_add_bookmark(const char *url, const char *title);
void browser_remove_bookmark(int index);
const char **browser_get_bookmarks(int *count);

/* History management */
void browser_add_history(const char *url);
void browser_navigate_history(int direction);
const char *browser_get_history(int index);

/* Download management */
void browser_add_download(const char *url, const char *filename);
void browser_remove_download(int index);
const char *browser_get_download_name(int index);
int  browser_get_download_progress(int index);
void browser_set_download_progress(int index, int pct);
int  browser_get_download_count(void);

/* Browser app structure for Black OS app system */
extern const struct app app_browser;

#endif /* BLACKOS_BROWSER_H */
