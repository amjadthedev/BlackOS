/* =============================================================================
 *  Black OS - Phase 8: application record contract (surfaces + callbacks)
 * ========================================================================== */
#ifndef BLACKOS_APP_H
#define BLACKOS_APP_H

#include "types.h"
struct window;                          /* defined in win.h */
/* Every GUI program is a pure callback struct: the compositor owns the
 * event pump and the chrome; the app renders into window->buf and reacts
 * to single keys.  Per-instance state hangs off window->priv.              */
struct app {
    const char *name;                                   /* start-menu entry */
    void (*open) (struct window *win);                  /* allocate priv    */
    void (*paint) (struct window *win, u32 *buf, int w, int h);
    void (*key)   (struct window *win, char c);
    void (*close) (struct window *win);                 /* free priv        */
};

/* Application instances (app_list.c links them into the start menu order). */
extern const struct app app_terminal;
extern const struct app app_taskmgr;
extern const struct app app_explorer;
extern const struct app app_editor;
extern const struct app app_netmon;
extern const struct app app_browser;
extern const struct app app_media;
extern const struct app app_settings;
extern const struct app app_store;
extern const struct app app_youtube;
extern const struct app app_snake;
extern const struct app app_pong;
extern const struct app app_code;
extern const struct app app_calc;
extern const struct app app_generic;
extern const struct app app_notes;
extern const struct app app_todo;
extern const struct app app_terminal;
extern const struct app app_clock;
extern const struct app app_about;

extern const struct app *const apps[];
extern const int app_count;
#endif /* BLACKOS_APP_H */
