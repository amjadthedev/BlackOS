/* =============================================================================
 *  Black OS - Windows 11-inspired shell (centered taskbar / start / tray)
 * ========================================================================== */
#ifndef BLACKOS_SHELL_H
#define BLACKOS_SHELL_H

#include "types.h"

struct shell_input {
    int mx, my;                         /* pointer position, px              */
    u8  buttons;                        /* mouse button mask (current)       */
    u8  click;                          /* 1 = fresh left-click edge         */
};

struct app;                             /* defined in app.h                  */

void shell_init(void);
/* Consume/collaborate: returns 1 when the shell absorbed the input.       */
int  shell_input(struct shell_input *ev);
/* Keyboard intake for the start-menu search; 1 = key consumed.           */
int  shell_key(char c);
/* Paints taskbar + open panels onto the framebuffer (last).               */
void shell_draw(void);
/* Start any registered app in a fresh window.                            */
void shell_launch_app(const struct app *a);

#endif /* BLACKOS_SHELL_H */
