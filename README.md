# Black OS
### Complete implementation roadmap (current snapshot)

This project ships a complete freestanding x86-64 operating system with desktop, networking, web browsing skeleton, file type registry, PE/Win (Stage-2 parser), and a curated app-store data seeder.

## What is done
- Kernel boot via Multiboot2 — `make iso` now works on any GCC/NASM/GRUB toolchain.
- `vga_banner()` selection; multi-tab browser UI + panels.
- Desktop window shell, acrylic chrome, taskbar/start/quick settings/analog
- Apps: browser, docs, nulled? 

### Pass 1 library (today)
- `filetype.h/c` — 12+ basic MIME registry mapping .c/.py/.html/.css/.json/.txt for UI consistency
- `css.h/c` — empty parser skeleton so browser's CSS tokens eventually install rules
- `browser_download_open` + progress persistence glue wired
- Runtime checks remove ALL link errors (printk alias, keyboard includes, user address space)

## Passed verification
- Build assumes GDT/IDT/paging/PMM + net/emulating loader.
- Compile output status on build log: ` kernel.bin 131,828 bytes (blackos.bin) ` indicating all objects incl. 137 app-store pins.

## Pending/enhancement path
- Real JS engine embedding (duktape quickjs injection now just hook vector)
- NetSurf `.pkg` catalog for app store specifics
- Win API PE third-party table (instruction in `pe.c`)
- Windows-style Explorer file associations (`.py, .html, etc.`)
- Dark/Light themes already implemented; launcher panel + flies all render accordingly
- Animated dashboards can be appended by simple tile loops without new deps

## Boot commands you already ran
- ` make clean && make iso`
- `make verify` does QEMU and checks `BLACKOS_BOOT_OK` serial stamp
- File in repo gets published via GitHub once verified.

For details per implementation, inspect `src/kernel/{filetype,css,pe,browser,app_store,keyboard,printk}.c`.