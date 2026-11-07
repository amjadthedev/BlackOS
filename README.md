# Black OS

An independent, from-scratch **x86_64** operating system that boots via the
Multiboot2 protocol and jumps straight from 32-bit protected mode into
**64-bit long mode**.  No Linux, no Windows, no libc — freestanding C plus
NASM assembly, packaged as a bootable `blackos.iso`.

```
GRUB2 (BIOS/UEFI) ──> Multiboot2 ──> 32-bit stub ──> EFER/PAE/cr0.PG
                                  └──────────> 64-bit kernel
```

## Feature summary

Black OS 1.0 — kernel-stage complete (incl. Phase 1–14 deliverables):

- Bootloader (MB2), paging, IDT/PIC/PIT, PS/2 kbd+mouse, RTC, serial mirror
- Graphical framebuffer (LFB RGBA8888), font, acrylic window manager
- Win11 shell: centered taskbar, start menu, quick settings
- VFS + FAT32 volume + package installer (`SYS_SOCKET`, HTTP fetch into `/apps`)
- IPv4 network stack: e1000, DHCP, DNS, HTTP, ICMP, firewall
- AC'97 audio, authentication + lock screen, theme engine, wallpaper
- Windowed apps: terminal, explorer, editor, browser, media, settings, store, games, code

## Browser (NetSurf-compat surface)

See `README_BROWSER.md` for the NetSurf browser wrapper with panels.
