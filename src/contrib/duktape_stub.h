/* =============================================================================
*  Black OS integration shim for duktape single-file JS VM (placeholder)
* ========================================================================== */
#ifndef BLACKOS_DUKTAPE_STUB_H
#define BLACKOS_DUKTAPE_STUB_H

#include "types.h"

typedef struct duk_angular_ctx { int dummy; } duk_context;

duk_context *duk_create_heap(void (*alloc)(void*), void (*free)(void*), void (*panic)(void*));
void duk_eval_string(duk_context *ctx, const char *src);
void duk_destroy_heap(duk_context *ctx);

#endif