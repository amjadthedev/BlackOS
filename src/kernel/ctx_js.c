/* =============================================================================
 *  Black OS - ctx_js.c : Duktape JS engine bridge
 *
 *  Owns the single long-lived duk_context used by the browser:
 *    - backed by the kernel heap (heap.c) so pages actually come from PMM
 *    - `print` global works out of the box (we register it once)
 *    - fatal errors unwind through abort() -> k_recover_env -> longjmp()
 *      instead of halting the machine
 *    - every evaluation is wrapped in a setjmp() recovery window so a wedged
 *      heap (OOM, internal assert) is torn down and rebuilt on next use
 * ========================================================================== */
#include "types.h"
#include "printk.h"
#include "io.h"          /* cpu_enable_sse(), hlt() */
#include "css.h"         /* css_init() */
#include <setjmp.h>
#include "../contrib/duk_config.h"
#include "../contrib/duktape.h"

/* heap_init() declared locally to avoid pulling heap.h (whose malloc() has
 * a usize prototype that collides with glibc stdlib.h from duk_config.h). */
extern void heap_init(void);

static duk_context *g_ctx;          /* NULL when not initialised          */
static jmp_buf g_recover;           /* armed around every JS call         */
static int g_js_inited;             /* 1 once heap is created             */
static int g_js_fatal;              /* sticky flag after a fatal          */

/* k_recover_env is the kernel's recovery window pointer (defined in kfmt.c) */
extern jmp_buf *k_recover_env;

/* kernel allocator wrappers (size_t ABI, matched to glibc declarations) */
static void *js_alloc(void *udata, size_t size)
{
    return malloc(size);
}

static void *js_realloc(void *udata, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void js_free(void *udata, void *ptr)
{
    (void)udata;
    if (ptr) free(ptr);
}

/* --------------------------------------------------------------- print ------- */
/* Duktape ships no built-in print; the embedder provides one.  Mirrors the
 * console rule: args space-separated, single trailing newline. */
static duk_ret_t ctx_js_print_fn(duk_context *ctx)
{
    int n = duk_get_top(ctx);
    for (int i = 0; i < n; i++) {
        if (i > 0)
            kputc(' ');
        const char *s = duk_safe_to_string(ctx, i);
        if (s)
            kputs(s);
    }
    kputc('\n');
    return 0;
}

/* -------------------------------------------------------------- allocators -- */
static void js_fatal(void *udata, const char *msg)
{
    (void)udata;
    kprintf("[js] FATAL: %s\n", msg ? msg : "(no message)");
    /* Longjmp back through the innermost armed window; when none is armed,
     * fall back to halting.  k_recover_env is provided by the host (kfmt.c). */
    if (k_recover_env) {
        jmp_buf *e = k_recover_env;
        k_recover_env = 0;
        longjmp(*e, 1);
    }
    for (;;)
        hlt();
}

/* ------------------------------------------------------------------- init --- */
void ctx_js_init(void)
{
    if (g_js_inited)
        return;

    /* PMM-backed arena; idempotent */
    heap_init();
    cpu_enable_sse();
    css_init();

    g_ctx = duk_create_heap(js_alloc, js_realloc, js_free, NULL, js_fatal);
    if (!g_ctx) {
        kprintf("[js] failed to create Duktape heap\n");
        return;
    }

    /* Global `print` function - matches what JS tests expect. */
    duk_push_c_function(g_ctx, ctx_js_print_fn, DUK_VARARGS);
    duk_put_global_string(g_ctx, "print");

    g_js_inited = 1;
    g_js_fatal = 0;
    kprintf("[js] Duktape %ld ready\n", (long)DUK_VERSION);
}

/* ------------------------------------------------------------- recovery ----- */
static void js_arm(void)   { k_recover_env = &g_recover; }
static void js_unarm(void) { k_recover_env = 0; }

void ctx_js_destroy(void)
{
    js_unarm();
    if (g_ctx) {
        duk_destroy_heap(g_ctx);
        g_ctx = NULL;
    }
    g_js_inited = 0;
}

/* ----------------------------------------------------------------- eval ------ */
/* Evaluate src; results of `print()` go to the console.  Never propagates a
 * JS exception past this boundary.  Returns 0 on success, -1 on JS error. */
int ctx_js_eval(const char *src, int len)
{
    if (!src || len <= 0)
        return -1;

    if (!g_js_inited)
        ctx_js_init();          /* lazy start (shell may run early)    */
    if (!g_js_inited || !g_ctx)
        return -1;

    if (setjmp(g_recover) != 0) {
        /* A fatal blew the heap out from under us: rebuild on next eval. */
        duk_destroy_heap(g_ctx);
        g_ctx = NULL;
        g_js_inited = 0;
        g_js_fatal = 1;
        js_unarm();
        ctx_js_init();          /* fresh heap for the next call        */
        return -1;
    }

    js_arm();
    duk_int_t rc = duk_peval_lstring(g_ctx, src, (duk_size_t)len);
    js_unarm();

    if (rc != DUK_EXEC_SUCCESS) {
        const char *err = duk_safe_to_string(g_ctx, -1);
        kprintf("[js] %s\n", err ? err : "(unknown error)");
        duk_pop(g_ctx);
        return -1;
    }

    duk_pop(g_ctx);
    return 0;
}

/* Shell-test helper: evaluate, print nothing on success. */
int ctx_js_eval_string(const char *src)
{
    /* length since no kernel string.h available here */
    int len = 0;
    while (src && src[len]) len++;
    return ctx_js_eval(src, len);
}
