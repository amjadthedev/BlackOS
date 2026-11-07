# Black OS

An independent, from-scratch **x86_64** operating system that boots via the
Multiboot2 protocol and jumps straight from 32-bit protected mode into
**64-bit long mode**.  No Linux, no Windows, no libc — freestanding C plus
NASM assembly, packaged as a bootable `blackos.iso`.

```
GRUB2 (BIOS/UEFI) ──> Multiboot2 ──> 32-bit stub ──> EFER/PAE/cr0.PG
                                  └──────────> 64-bit kernel: banner, IDT,
                                                PIC/PIT, PS/2 keyboard,
                                                bitmap PMM, dynamic paging,
                                                interactive shell
```

## Feature summary

| Phase | Deliverable | Where |
|------|-------------|-------|
| 1 | Multiboot2 header + protected→long mode transition | `src/boot/boot.s` |
| 2 | Linker script + build automation | `linker.ld`, `Makefile` |
| 3 | VGA text driver (black bg / neon cyan, banner, cursor) | `src/kernel/vga.c`, `printk.c` |
| 4 | IDT, exception dump, PIC remap, PIT tick, PS/2 keyboard | `idt.c/h`, `isr.s`, `pic.c/h`, `keyboard.c/h` |
| 5 | Bitmap PMM (multiboot2 mmap) + dynamic 2 MiB paging | `pmm.c/h`, `paging.c/h` |
| 6 | GRUB menu (BIOS+UEFI hybrid ISO) + QEMU harness + verification | `iso/boot/grub/grub.cfg`, `scripts/` |

At boot Black OS prints the BLACK OS banner, installs the IDT, remaps the
PIC, starts a 1 ms PIT tick, parses the multiboot2 memory map, establishes
the bitmap physical allocator (with a selftest), reports the MMU state
(CR0.PG / PAE / EFER.LME / LMA / CR3), then drops into an interactive
console: `[black@blackos /]#`.

Shell commands: `help`, `clear`, `info`, `mem`, `uptime`, `banner`,
`pmmtest`, `maptest`, `reboot`, `halt`.

Every print is mirrored to **COM1**, so the whole boot and console session
can be verified headlessly — see `scripts/verify.py`.

## Repository layout

```
blackos/
├── Makefile                build automation (kernel.bin, ISO, run, verify)
├── linker.ld               kernel memory layout (load 1 MiB)
├── Dockerfile              reproducible build + test environment
├── bochsrc.txt             Bochs alternative-VM configuration
├── iso/
│   └── boot/grub/grub.cfg  boot menu (normal / recovery / safe TTY)
├── src/
│   ├── boot/boot.s         MB2 header + long-mode entry + stack/page tables
│   └── kernel/             freestanding C kernel
│       ├── types.h  io.h  multiboot2.h  string.h/c
│       ├── printk.h/c  serial.h/c  vga.h/c
│       ├── idt.h/c  isr.s  pic.h/c  keyboard.h/c
│       ├── pmm.h/c  paging.h/c
│       └── kernel_main.c   entry point + interactive shell
└── scripts/
    ├── run.sh              QEMU harness (KVM auto, headless, gdb modes)
    ├── mkiso.sh            xorriso fallback packer
    └── verify.py           automated boot + console audit (exit 0/1)
```

## Toolchain

Requirements on a Linux build host:

- `nasm` (2.15+)
- a 64-bit freestanding-capable C compiler: `x86_64-elf-gcc` (canonical
  OSDev choice) **or** `x86_64-linux-gnu-gcc` (any modern GCC works, the
  kernel is freestanding).  Native `gcc` on a 64-bit Linux host works too.
- `grub-mkrescue` (grub-common), `grub-pc-bin` (BIOS image),
  `grub-efi-amd64-bin` (UEFI spare), `xorriso`, `mtools`
- `qemu-system-x86_64` for the test harness (or Bochs with `bochsrc.txt`)

The included `Dockerfile` provisions all of the above on any host (including
Windows/macOS):

```bash
docker build -t blackos-build .
```

## Build, run, verify

```bash
# inside the container (or a Linux host with the packages above)
cd /black                      # or the mounted blackos checkout
chmod +x scripts/*.sh scripts/*.py    # only needed for volume-mounted trees
make                           # -> build/kernel.bin
make iso                       # -> blackos.iso (BIOS + UEFI hybrid)

make run                       # QEMU window, serial captured to build/serial.log
make nographic                 # headless; serial console on stdio (Ctrl-A x when typing)
make verify                    # automated Phase-8-style audit; exit code 0 = PASS

./scripts/run.sh --debug       # same VM + gdbstub on tcp::1234
```

QEMU harness defaults: `-m 512`, CD boot, KVM enabled automatically when
`/dev/kvm` is available (`-cpu host`), TCG (`-cpu max`) otherwise.

Manual specification-compliant invocations, for the record:

```bash
# ISO (grub-mkrescue path)
grub-mkrescue -o blackos.iso iso/ --compress=no

# ISO (pure xorriso path, after one grub-mkrescue run materialised eltorito.img)
xorriso -as mkisofs -R -r -J -V BLACK_OS \
        -b boot/grub/i386-pc/eltorito.img \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        -o blackos.iso iso/

# Interactive test run
qemu-system-x86_64 -enable-kvm -cpu host -m 512 -cdrom blackos.iso -serial file:build/serial.log
```

## Verification contract

`scripts/verify.py` boots the ISO headless (`-display none -serial stdio`)
and asserts, over the mirrored serial console:

1. the `BLACKOS_BOOT_OK` marker printed at the end of kernel bring-up,
2. boot telemetry — multiboot magic, PMM mapper selftest (`selftest OK`),
   live page-table root (`CR3 root`), identity map (`0x0 - 0x3fffffff`),
3. interactive shell behaviour — `info`, `mem`, `uptime`, `pmmtest`,
   `maptest` are typed through the serial pipe and their responses matched.

Exit code 0 means the full boot chain plus the console I/O path is healthy.

## Design notes / current limits

- Identity map: first 1 GiB statically (512 × 2 MiB pages).  Higher
  addresses are added on demand via `paging_map_2mb()` (its demo is the
  shell `maptest` command mapping 0x40000000).
- PMM inventory covers the first 1 GiB — matching the mapping.  Frames are
  handed out lowest-first so newly created page tables are always inside
  the identity window (a 64-bit C model choice).
- SMP, userspace, disk/NET drivers and a virtual filesystem are the next
  development phases; the PMM is deliberately structured to back a buddy
  allocator + kernel heap (`kmalloc`) later.
- `__kernel_start` / `__kernel_end` keep the relocated image and the PMM
  bitmap itself out of the free-frame pool.
