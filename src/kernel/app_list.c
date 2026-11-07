/* =============================================================================
 *  Black OS - start-menu application registry
 * ========================================================================== */
#include "app.h"

const struct app *const apps[] = {
    &app_terminal,
    &app_taskmgr,
    &app_explorer,
    &app_editor,
    &app_netmon,
    &app_browser,
    &app_media,
    &app_settings,
    &app_store,
    &app_youtube,
    &app_snake,
    &app_pong,
    &app_code,
    &app_calc,
    &app_generic,
    &app_notes,
    &app_todo,
};

const int app_count = (int)(sizeof(apps) / sizeof(apps[0]));
