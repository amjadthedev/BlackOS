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
