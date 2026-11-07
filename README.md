# Black OS

An independent, from-scratch **x86_64** operating system that boots through
the Multiboot2 protocol and runs directly from real hardware (BIOS+UEFI hybrid
GRUB image). Free-standing C + NASM, packaged as `blackos.iso`.

## Quick status (Nov 2026)

- ✅ Kernel boots via MB2, boots kernel, enters long mode, renders VGA
- ✅ `make iso` & `make verify` are both registered targets
- ✅ 5.MB image produced (BIOS + UEFI hybrid)
- ✅ `BLACKOS_BOOT_OK` printed to serial on successful boot
- 🚧 Linux binary support (Strategy-1 'ELF' + syscall)". Still pending verification
- 🌐 **Web Browser**: multi-tab, panels (bookmarks/history/downloads), address bar editability, network status chip
- 🔧 **App store catalog**: 137 items (apps + games, unique entries).
- ⏰ **Real-time UI**: windowing compositor (drag/edge-snap/FancyZones-style preview), desktop brand, Taskbar, Start Menu, Quick Settings, wallpaper gradient, and active theme accent
- 🎮 **Games**: Snake & Pong (built-in native)
- 🧑‍💻 **Settings**: theme panel + account + volume stub + network diagnostics

## Runtime & images

- **Build**: `make clean && make iso && make verify`
- **Boot**: Hyper-V Gen 1 VM recommended (bios boot), 512 MB + 1 vCPU, SecureBoot off
- **Runtime smoke test**: `make nographic` (headless over serial)
- **Debug**: `make verify` (QEMU headless audit, exit 0 = PASS)

## Feature overview

### Kernel core
- Multiboot2 header, 32→64 bit transition, page 4KiB + dynamic 2MiB paging
- Bitmap PMM (free-frame inventory) + `pmm_selftest()`
- IDT/IRQ/PIC/PIT (1ms uptime tick)
- Keyboard (scancode 1 translate), PS/2 mouse, RTC, COM1 mirror

### Display & shell
- 32bpp LFB (RGB), 8×8 font subsystem
- Rounded rects, box blur, alpha blend (`fb_blend`/`fox` primitives), drop shadows
- Win11 shell: centered taskbar, start menu w/ search, quick settings, tray chips (weather + clock)

### Window & apps
- 8-window table + z-order, drag move, preserve/resize bounds
- App registry (`app_list.c`); apps by `.name`: terminal, explorer, editor, browser, media, settings, store, games, code, generic, calc, notes, taskmgr, netmon, youtube
- App install store fetches `.pkg` over HTTP to `/apps/` and launches via ring-3 (`syscall_run_task`)

### Networking / stacks
- e1000 + ARP/IP/ICMP/UDP/TCP/HTTP; DHCP bind + DNS
- Firewall (dir allow/deny rules), inbound toggle
- Weather widget (DNS → HTTP JSON) for tray

### Audio
- AC'97: software beeps (440Hz), volume control UI stub

### Account / security
- Salted FNV hashes, default `black`, no password on first login, lock screen path

### Themes
- themes_init / theme_count / apply_theme target globals (`g_accent`, `g_panel_bg`, `g_text_bright`)
- Presets: Black Acrylic, Mica Blue, Neon Cyan, Dark Rose, Forest, Midnight

### Multi-tab browser
- URL bar editing, per-tab open/switch/close (`n`, 1..8)
- Panels: `b` bookmarks, `y` history, `d` downloads
- Bookmarks / history / downloads state
- Network status chip; Web button hooks (JS/CSS still phase 1)

### Linux binary compat (Strategy 1)
- ELF64 parser with load bias + entry offset
- Syscall mapping (read/write/mmap/exit/exit_group)
- Phase 1 scope shims only; real files/POSIX FDs remain roadmap

### Compiler & toolchain notes
- Freestanding C + NASM only; no libc.
- Required tools: `nasm`, `x86_64-linux-gnu-gcc` (x86_64-elf-gcc), `grub-mkrescue`, `xorriso`, `qemu-system-x86_64`
- QEMU build sanity note for Hyper-V: boot via **Gen 1** VM, CD emulation, serial mirror via COM1
- `Makefile` CONFIG_TOOLCHAIN banner warn when missing

### Documentation
- `README_BROWSER.md` - browser integration + controls
- `NETSURF_*` docs (\*.md) - integration plan / status
- `BROWSER_INTEGRATION_STATUS.md`, `SUMMARY.md` - current delta & next steps

## Roadmap / Work in progress (explicit)
- **Full JS/CSS engine** → deferred to OSDev external-session; hooks currently no-op skeleton in browser (browser_js_eval, browser_css_apply)
- **Web rendering layer** (NetSurf in `libnsfb`) staged at panel outputs, not full.
- **Linux binary runtime body** → `SYS_READ_FILE` + path ops only today; POSIX FD real mapping, `open(2)` not wired (returns -ENOSYS)
- **Download progress pump** (bandwidth tracking) wired as scaffold at 25% to be replaced with real netpoll single-pull.
- **Windows PE loader strategy 2** postponed (not started).

## Getting it to launch
```bash
# Toolchain on Ubuntu WSL:
sudo apt install build-essential nasm grub-pc-bin xorriso mtools qemu-system-x86
cd /mnt/c/Users/mukrr/blackos
make clean && make iso && make verify   # verify boots until BLACKOS_BOOT_OK serial
```
Boot the produced `blackos.iso` in Hyper-V Gen 1 guest (512 MB, 1 vCPU), only after sequential `verify` reports pass.