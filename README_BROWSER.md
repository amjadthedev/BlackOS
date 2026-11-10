# Black OS Browser — Integration Guide

## Overview

Black OS ships a NetSurf-based web browser (`browser.c`) that renders into
per-window pixel surfaces managed by the Phase 8 window manager. The browser is
registered in the start menu and launched via `gui` from the interactive
console or by clicking its taskbar icon.

## Files

| File | Role |
|------|------|
| `src/kernel/browser.h` | Public API (tabs, bookmarks, history, downloads) |
| `src/kernel/browser.c` | Implementation |
| `src/kernel/app_browser.c` | App registration + fallback entry point |
| `src/kernel/app_list.c` | Start-menu registry |

## Browser Controls

| Key | Action |
|-----|--------|
| `n` | New tab |
| `1`–`8` | Switch to tab 1–8 |
| `f` | Focus address bar (then type URL, Enter to navigate) |
| `r` | Refresh page |
| `h` | Go home |
| `b` | Toggle bookmarks panel |
| `y` | Toggle history panel |
| `d` | Toggle downloads panel |

## Architecture Notes

The browser state lives in `static struct browser_window g_browser`. Each open
window points its `win->priv` at `&g_browser`; all app callbacks operate on
that single shared instance regardless of how many windows are open.

Multi-tab support stores `tabs[8][256]` URLs and `tab_titles[8][128]` titles.
Panel visibility is encoded as `panel_visible` (0 none / 1 bookmarks / 2
history / 3 downloads).

Download progress is stored per-download as `downloads_progress[8]` (0–100 ints)
and rendered as mini progress bars inside the downloads panel.

Bookmark display titles are stored separately in `bookmarks_title[16][128]` so
adding a bookmark never clobbers the active page title.

## Headless Self-Tests (`webtest` / `csstest`)

The browser pipeline can be exercised from the serial console without a window
manager, which is how `scripts/verify.py` validates the feature on each boot:

| Command | What it does |
|---------|--------------|
| `webtest` | Loads the built-in demo page (`<title>`, `<style>`, `<script>`), runs the JS engine, prints `[js] js ok: 42` and the CSS-match result |
| `csstest` | Parses the built-in demo CSS, prints `[css] parsed 3 rules (3 total)` |

Both are registered as interactive console commands in `kernel_main.c` and are
covered by `CMD_ASSERTIONS` in `scripts/verify.py`, so `make verify` fails if
the JS or CSS engine regresses.

## JavaScript Engine

Pages are run through an embedded Duktape 2.7 interpreter, bridged from
`src/kernel/ctx_js.c`:

- **Backend**: allocations go through the kernel heap (`heap.c`, 8 MiB
  PMM-backed arena), so JS memory pressure is visible in `mem` and covered by
  the heap self-tests.
- **Crash isolation**: every evaluation runs inside a `setjmp` recovery window
  (`js_arm`/`js_unarm`). A Duktape fatal (heap OOM, internal assert, deliberate
  `abort()` from the JS `print` path) unwinds to the recovery point instead of
  halting the machine; the next evaluation lazily rebuilds a fresh heap.
- **`print(...)`**: `console.log`-style global; prints space-separated arguments
  to the kernel console.
- **Dates**: the Date builtin is enabled with an RTC-backed, UTC-only
  `gettimeofday` provider (`kfmt.c`); no libc `mktime`/`strftime`/`strptime` is
  linked.
- **SSE**: Duktape object files are compiled with `-msse2 -mfpmath=sse` while
  the rest of the kernel uses `-mno-sse`; `cpu_enable_sse()` is called early in
  `kmain` and again in `ctx_js_init`.
- **Boot check**: `ctx_js_init` is idempotent; `[js] Duktape 2.7 ready` is logged
  once at boot before the shell starts.

## CSS Engine

`src/kernel/css.c` parses `<style>` blocks (`browser_css_apply`) and tracks
rules such as `body { color: #...; background-color: #...; }`. Currently the
computed body color drives the demo-page surface hint; the rule table is a
foundation for layout and per-element styling.

```c
u32 css_body = css_match_color("body", 0xE0E0E0);   /* parse status + color */
```

## Memory Notes

`heap.c` is a first-fit, address-ordered allocator with block splitting and
back-off coalescing, used by both the JS engine and the CSS parser. Zero bytes
are handed back out (payload is memset on allocation), and double-frees/foreign
pointers are detected by a `HEAP_MAGIC` tag check with a diagnostic rather than
a corruption.
