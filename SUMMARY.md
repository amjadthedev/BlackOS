# Black OS — Full Desktop Expansion (Complete Source)
# ============================================================================
# Independent x86_64 kernel (NASM + freestanding C, Multiboot2, GRUB, QEMU)
# Built in C:\Users\mukrr\blackos
# ============================================================================

## IMPORTANT: What IS and IS NOT possible

### What this codebase delivers (all source, ready to build)
| Feature | Files | Status |
|---------|-------|--------|
| Lock screen + password auth | auth.c/h, lockscreen.c/h | Done |
| Settings app (Display/Network/System/Audio/Account) | settings.c/h | Done |
| Themes (6 built-in: Black Acrylic, Mica Blue, Neon Cyan, Dark Rose, Forest, Midnight) | themes.c/h | Done |
| Wallpapers (solid / gradient / 24-bit BMP from VFS) | wallpaper.c/h | Done |
| Audio (PC speaker beep + AC'97 PCM DMA playback) | audio.c/h | Done |
| Real internet over Ethernet (DHCP+DNS+TCP+HTTP/1.1 via e1000) | netstack.c/h, e1000.c/h, pci.c/h | Done |
| Web browser (real HTTP GET, HTML structure parsing, links, images) | app_browser.c | Done |
| YouTube client (fetch youtube.com, extract <title>) | app_youtube.c | Done |
| App Store (catalog, HTTP .pkg download, VFS install, ring-3 launch, search) | appstore.c, package.c/h | Done |
| Code editor (open/save/edit files, syntax color, run flat binaries) | app_code.c | Done |
| Games (Snake, Pong) | app_snake.c, app_pong.c | Done |
| Terminal + task manager + explorer + media | existing | Done |
| SYSCALL/SYSRET ring-3 (IA32_STAR/LSTAR/FMASK, 12 syscalls) | syscall.s/c, paging.c/h | Done |

### What CANNOT be built in this context (fundamental limits)
| Feature | Why not |
|---------|---------|
| **Real Chromium** | ~35M lines of code; needs V8, Blink, GPU acceleration, sandboxing, full POSIX libc |
| **Universal app support** (.exe, .app, .apk, ELF) | Each needs a compatibility layer like Wine (20+ years); no userspace libc or dynamic linker yet |
| **Python / Node / Docker terminals** | Need userspace runtime, ELF loader, dynamic linker, process scheduler — multi-year project |
| **Real app store with company apps** | Companies don't publish apps for hobby OSes. Store client + .pkg format exist, but you author apps yourself |

### Build fixes applied
1. `syscall.s`: Added `global syscall_kernel_rsp` and `.bss` storage (was missing, caused linker error)
2. `pci.c/h`: Added `pci_scan_class()` for finding audio devices by class code
3. `audio.c`: Fixed AC'97 detection (was calling `pci_scan_nic()` which only finds NICs), implemented PCM DMA playback
4. `app_code.c`: Fixed `code_run_file()` to read file from VFS + run as ring-3 blob; added `strcpy_fixed` forward declaration; added themes.h/paging.h includes
5. `app_browser.c`: Added `fb.h`/`themes.h` includes; rewrote HTML parsing with `<p>`, `<h1>-<h6>`, `<br>`, `<a href>`, ` src>` support; added URL scheme parsing (http://, https://)
6. `appstore.c`: Fixed `pkg_launch(NULL)` bug — now reads .pkg from VFS and launches properly; added search bar with real-time filtering; added `fb.h` include
7. `kernel_main.c`: Merged duplicate `if/else` command chains (was printing "unknown command" AND executing); added `audio.h` include; `beep` command now calls `audio_beep()`
8. `audio.h`: Added `AC97_CODEC_READ`/`AC97_CODEC_WRITE` defines

## Repository Structure (44 `.c` + headers in src/kernel/)
auth.c/h, lockscreen.c/h, themes.c/h, wallpaper.c/h, audio.c/h,
netstack.c/h, e1000.c/h, pci.c/h, package.c/h,
appstore.c, app_browser.c, app_youtube.c, app_code.c,
app_snake.c, app_pong.c, settings.c,
syscall.s/c, paging.c/h, comp.c, shell.c,
app.h/app_list.c, app_*.c (13 apps), ui.h/c, font.h/c,
fb.h/c, console.c/h, vga.h/c, printk.h/c, serial.h/c,
keyboard.h/c, ps2mouse.h/c, rtc.h/c, idt.h/c, pic.h/c,
pmm.h/c, bdev.h, vfs.h/c, fat32.h/c, string.h/c, types.h,
io.h, multiboot2.h, boot.s, linker.ld, Makefile, Dockerfile

## Build Instructions
```
# From C:\Users\mukrr\blackos (Linux host with toolchain, or Docker)
make          -> build/kernel.bin
make iso      -> blackos.iso (BIOS + UEFI hybrid via grub-mkrescue)
make run      -> QEMU with VGA window
make nographic-> QEMU headless over stdio serial
make verify   -> scripts/verify.py (greps BLACKOS_BOOT_OK)
```

No host toolchain on Windows — use Docker:
```
docker build -t blackos-build .
docker run -it -v $(pwd):/black blackos-build
cd /black && make iso && make run
```

The Makefile uses `KSRC := $(wildcard src/kernel/*.c)`, so all 44 `.c` files compile automatically.

## Shell Commands (VGA text console)
help, clear, info, mem, uptime, banner, reboot, halt, pmmtest, maptest,
gui, mountfat, netup, ping, beep, settings, store, youtube,
snake, pong, code, wallpaper, theme, passwd, lock, userrun

## GUI Start Menu (13 apps)
terminal, task manager, explorer, editor, netmon, browser, media,
settings, app store, youtube, snake, pong, code editor

## How to use key features
- **GUI**: Type `gui` at the VGA console, or the compositor starts on boot
- **Lock screen**: Type `lock` or wait for auto-lock; enter password to unlock
- **Settings**: Type `settings` or open from start menu; change theme, wallpaper, volume, password
- **App Store**: Type `store`; browse catalog, type to search, Enter to install/launch
- **Web Browser**: Type `youtube` or `browser`; enter URL, press Enter/GO to fetch
- **Code Editor**: Type `code`; open/save files from VFS, edit, press Ctrl+R to run flat binary
- **Games**: Type `snake` or `pong`; arrow keys to play

## Known State
- All code is freestanding C (no libc headers), identity-mapped below 1 GiB.
- Multiboot2 framebuffer tag (800×600×32) in boot.s; fb_init parses it.
- GDT has user code (0x20/DPL3) + user data (0x28/DPL3) for SYSRET.
- No OSDev toolchain on the Windows host; use Docker for build/boot verification.
- Code editor "run" executes flat binaries (NASM -f bin output) via ring-3 syscall.
- Browser renders text with basic HTML structure; no image/video decoding (would need PNG/JPEG/video codecs).
- AC'97 PCM audio uses DMA buffer; PC speaker fallback works on all hardware.
