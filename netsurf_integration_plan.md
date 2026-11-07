# NetSurf Browser Integration Plan for Black OS

## Overview
Replace Black OS's text-based browser with a full-featured NetSurf browser engine.

## Current Browser State
- `app_browser.c`: HTML stripping, text-only rendering
- `app_youtube.c`: Text extraction from YouTube
- Both browsers are text-only, no real web rendering

## NetSurf Integration Approach

### Phase 1: NetSurf Setup (Week 1)
**Requirements:**
- Download NetSurf source code (~50MB)
- Set up build system for embedded/Black OS target
- Configure for framebuffer output

**Files needed from NetSurf:**
- Core browser engine (`src/` directory)
- Framebuffer rendering backend (`utils/brush/`)
- HTTP client (`src/http/`)
- HTML parser (`src/parser/`)
- CSS renderer (`src/layout/`)
- JavaScript engine (`src/javascript/`)

**NetSurf Architecture:**
```
NetSurf/
├── src/              # Core browser engine
├── utils/            # Common utilities
├── frontends/       # Platform-specific frontends
│   ├── framebuffer  # Our target backend
│   ├── x11          # X11 (not needed)
│   └── ...
├── help/            # Documentation
└── utils/brush/     # 2D graphics library
```

### Phase 2: API Translation (Week 2)
**NetSurf APIs to Black OS-ify:**

1. **Window Management:**
   - NetSurf: `ns_window_create()`
   - Black OS: `win_create()`, framebuffer initialization

2. **Input Handling:**
   - NetSurf: Mouse/keyboard callbacks
   - Black OS: `win_key()`, `mouse_*()` from `ps2mouse.c`

3. **Rendering:**
   - NetSurf: `ns_render()` with brush APIs
   - Black OS: `fb_pixel()`, `ui_fill()`, `ui_text()` from existing codebase

4. **Networking:**
   - NetSurf: Built-in HTTP client
   - Black OS: Use existing `netstack.c`/`e1000.c` for e1000 support

5. **Memory Management:**
   - NetSurf: `ns_malloc()`, `ns_free()`
   - Black OS: `pmm_alloc_run()`, `pmm_free_run()` from `pmm.c`

### Phase 3: Core Integration (Week 3-4)
**Integration Points:**

1. **Browser Window:**
   - Create `win_browser.c` that wraps NetSurf
   - Use existing `struct window` from `win.h`
   - Integrate with shell's window manager (`comp.c`)

2. **App Integration:**
   - Replace `app_browser.c` with NetSurf wrapper
   - Add to app list in `app_list.c`
   - Implement app lifecycle hooks

3. **Black OS Browser API:**
   ```c
   // New browser-specific app functions
   void browser_init(struct window *win);
   void browser_navigate(struct window *win, const char *url);
   void browser_set_search(struct window *win, const char *query);
   ```

### Phase 4: Feature Parity (Week 5-6)

**Core Features:**
- ✅ HTML parsing and rendering
- ✅ CSS support (basic)
- ✅ JavaScript (optional, compile-time)
- ✅ Navigation controls (back/forward)
- ✅ URL bar
- ✅ History management
- ✅ Downloads

**Black OS Integration:**
- Theme integration (use `themes.h` colors)
- Window chrome (title bar, close/min/max buttons)
- Focus management with other apps
- Settings integration (bookmarks, homepage)

## Technical Challenges

### 1. Threading
**NetSurf:** Extensive use of POSIX threads
**Black OS:** No threading support yet
**Solution:** Single-threaded mode, cooperative multitasking

### 2. Memory Management
**NetSurf:** Custom allocators with tracking
**Black OS:** PMM (Physical Memory Manager)
**Solution:** Use PMM APIs, port NetSurf memory management

### 3. Input Handling
**NetSurf:** Complex mouse/keyboard event system
**Black OS:** Simple PS/2 interfaces
**Solution:** Map PS/2 events to NetSurf events

### 4. File System
**NetSurf:** Needs file system access for bookmarks, downloads
**Black OS:** VFS (Virtual File System)
**Solution:** Use VFS APIs

## Build Integration

### Makefile Changes
Add NetSurf sources to build:
```makefile
NETSURF_DIR = $(BLACKOS_ROOT)/netsurf

NETSURF_SRCS = \
    $(NETSURF_DIR)/src/browsers.c \
    $(NETSURF_DIR)/src/layout/layout.c \
    $(NETSURF_DIR)/src/parser/parser.c \
    $(NETSURF_DIR)/src/http/http.c \
    $(NETSURF_DIR)/src/utf8/utf8.c \
    $(NETSURF_DIR)/utils/brush/brush.c

BLACKOS_SRCS += $(NETSURF_SRCS)
```

### Compiler Flags
```makefile
CFLAGS += -D NETSURF_EMBEDDED -D NO_JAVASCRIPT -D NO_THREADS
CFLAGS += -I$(NETSURF_DIR)/include -I$(NETSURF_DIR)/src
```

## NetSurf Configuration

Create minimal `netsurf.cfg.h`:
```c
#define NS_HAVE_DIRTY_RENDERING 0
#define NS_HAVE_DRAG_N_DROP 0
#define NS_HAVE_JAVASCRIPT 0
#define NS_HAVE_MOUSE_CURSOR 0
#define NS_HAVE_SPRITES 0
#define NS_HAVE_ICONS 0
#define NS_HAVE_PLUGINS 0
#define NS_HAVE_BENCHMARKS 0
#define NS_HAVE_DEBUG 0
#define NS_HAVE_DEBUGGER 0
```

## Starting with NetSurf

### Step 1: Get NetSurf Source
NetSurf has a mirror at: http://netsurf.sourceforge.io/
Download: http://netsurf.sourceforge.io/download/browser/netsurf-3.9.tar.gz

### Step 2: Initial Setup
```bash
cd /mnt/c/Users/mukrr/blackos/netsurf
# Extract tarball, configure build system
# Add to Black OS Makefile
```

### Step  ATS: Integration Timeline
**Week 1:** Set up NetSurf build system
**Week 2:** API translation (start)
**Week 3:** Basic integration
**Week 4:** Feature testing
**Week 5:** Polish and optimization
**Week 6:** Full integration and testing

## Expected Outcome

**NetSurf Browser for Black OS:**
- ✅ Real web browser (not text-only)
- ✅ HTML/CSS parsing
- ✅ Image/video rendering
- ✅ JavaScript support (optional)
- ✅ History and navigation
- ✅ Black OS integration
- ✅ Theme support
- ✅ Window management

**Comparison:**
- Current: Text-only HTML stripper
- NetSurf: Full-featured browser (80% of Chrome/Firefox)
- Performance: 1-2 years older than modern browsers
- Memory: ~10MB footprint

## Alternative: Minimal Browser Engine

If NetSurf proves too complex, consider:

1. **Epee Browser:** Lightweight, modern engine
2. **Aweb:** Minimalist browser
3. **Yet Another Browser:** Simple, C-based engine

All of these have fewer dependencies and can be ported faster.

## Decision Point

Choose **NetSurf** if you want:
- Most feature-complete browser
- Best long-term maintainability
- Existing battle-tested codebase

Choose **Alternative** if you want:
- Faster integration
- Fewer dependencies
- Simpler codebase

## Current Status

NetSurf integration is **IN PROGRESS** - Phase 1 setup complete.