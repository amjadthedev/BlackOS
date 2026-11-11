# Booting Black OS on Real Hardware & Virtual Machines

`blackos.iso` is a **BIOS + UEFI hybrid ISO**. It boots from:

- BIOS / CSM legacy mode (GRUB i386-pc, Multiboot v1)
- UEFI without Secure Boot (GRUB x86_64-efi, Multiboot v1)
- QEMU (verified), VirtualBox, VMware, Hyper-V Gen1 (BIOS) and Hyper-V Gen2 (UEFI)
- Real 2010s–2020s era PCs that have legacy/CSM mode or UEFI with Secure Boot **off**

## Quick start by platform

| Platform | How to boot |
|----------|-------------|
| **QEMU (BIOS)** | `qemu-system-x86_64 -m 512 -cdrom blackos.iso` |
| **QEMU (UEFI/OVMF)** | Use pflash: `OVMF_CODE_4M.fd` (readonly) + `OVMF_VARS_4M.fd` as VARS; `-cdrom blackos.iso` |
| **VirtualBox** | Attach `blackos.iso` to a VM's DVD drive. If the VM is BIOS, just boot. If UEFI, disable Secure Boot. Use an Intel e1000 NIC if you want network. |
| **VMware** | Same: attach to CD/DVD. For UEFI, turn Secure Boot **off**. |
| **Hyper-V Gen1** | Attach ISO to the DVD; generation 1 is BIOS, no Secure Boot concern. |
| **Hyper-V Gen2** | **Secure Boot must be OFF** (Black OS GRUB is unsigned). Attach ISO to DVD; boot. The legacy "Synthetic" NIC is not used. |
| **Real PC (BIOS)** | Burn/copy to USB stick or CD; boot it. PS/2 keyboard required for the console. |
| **Real PC (UEFI only)** | Enter the firmware setup, disable **Secure Boot**, boot the ISO/USB. CSM mode also works if your firmware still has it. |

## What works on real hardware (Phase 1)

- ✅ **Boot reaches the interactive console** — VGA text mode works everywhere (firmware sets it up).
- ✅ **Power off (`halt`)** — ACPI S5 is written to PM1a/PM1b control; also works from the Settings → System shutdown button and the shell quick-settings power tile.
- ✅ **Reboot** — 8042 reset pulse (works on every x86 PC).
- ✅ **PS/2 keyboard** — 2010s+ PCs still ship PS/2 or via CSM. USB-only machines are **not** supported yet (USB HID is Phase 2).

## What does NOT work yet (Phase 1 scope)

- ❌ **USB keyboard/mouse** — xHCI/UHCI HID driver is not written. On a USB-only machine you can boot but cannot type.
- ❌ **Disk persistence** — no AHCI/NVMe driver yet; the kernel is volatile.
- ❌ **Network** — e1000 is the only supported NIC (implemented in-tree; verified against QEMU's emulation). Hyper-V synthetic NICs require a future NetVSC driver.
- ❌ **Audio** — no AC'97/HDA driver yet; Settings → Audio is a stub.
- ❌ **Secure Boot** — GRUB in the ISO is unsigned. Secure Boot must be OFF. Signing is a separate long-term project.
- ❌ **PCIe passthrough / modern drivers** — out of Phase 1 scope.

## Hyper-V Gen2 step-by-step

1. Windows → Hyper-V Manager → **New → Virtual Machine…**
2. Generation: **Generation 2**, memory 512 MB+.
3. Under **Settings → Security** → **Secure Boot**: uncheck **Enable Secure Boot**.
4. Settings → **DVD Drive** → insert `blackos.iso`.
5. Start. You should see the GRUB menu, then Black OS boot text on the VM screen.
6. When the console appears, type `help` (pressing **Enter** works once the boot ISO is loaded).

> If you boot with Secure Boot ON, the VM will refuse to load the unsigned GRUB. That is expected.

## The `halt` command

Typing `halt` at the console now powers the VM/PC off via **ACPI S5**:

1. Kernel scans EBDA → 0xE0000–0xFFFFF, then the first 1 GiB of RAM for the RSDP.
2. Walks RSDT/XSDT to find the FADT ("FACP") and the DSDT.
3. Extracts the `_S5_` package from the DSDT; writes `SLP_EN|SLP_TYP` to PM1a_CNT (and PM1b_CNT when present).
4. If anything fails along the way, the kernel falls back to the legacy QEMU ports (`0x604`/`0xB004`) so headless QEMU runs still behave.

On BIOS firmware-based machines the RSDP is normally found in EBDA or the legacy ROM window. On UEFI firmware (Hyper-V Gen2, OVMF) it is found by the RAM fallback scan.

## Why real hardware may still not fully work

Black OS is a research hobby kernel. Its console is VGA-text; its USB stack does not exist yet; its disk driver has been replaced by RAM-backed VFS. A real PC with no PS/2 port and no legacy video mode is effectively **unbootable-in-practice** until Phase 2 (USB HID + PS/2 emulation on modern chipsets).

## Credits and attribution

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and **Settings → About** inside Black OS.

## Verification

- `make verify` — boot smoke test (QEMU, BIOS). Drives the serial console (`info`, `mem`, `uptime`, `pmmtest`, `maptest`, `webtest`, `csstest`, `halt`) and asserts the expected responses.
- OVMF UEFI test (manual): see `build/uefi_test.log` after running the OVMF pflash command; it must contain `BLACKOS_BOOT_OK`.

### OVMF test recipe

```sh
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/OVMF_VARS.fd
timeout 60 qemu-system-x86_64 -m 512 \
  -drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS.fd \
  -cdrom blackos.iso -display none -serial stdio -no-reboot
```

A passing log ends with `[acpi] FADT at 0x... DSDT at 0x...` and `BLACKOS_BOOT_OK`.
