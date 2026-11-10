# =============================================================================
#  Black OS - Phase 2: build automation (NASM + 64-bit cross GCC + GRUB)
#
#  Targets:
#     make          -> build/kernel.bin (raw kernel binary)
#     make iso      -> blackos.iso   (BIOS + UEFI hybrid via grub-mkrescue)
#     make run      -> boot the ISO in QEMU with a VGA window
#     make nographic-> QEMU headless over stdio serial
#     make verify   -> automated boot verification (Phase 8-style harness)
#     make clean
#
#  Toolchain order of preference: x86_64-elf-gcc, x86_64-linux-gnu-gcc,
#  x86_64-pc-linux-gnu-gcc (native on a 64-bit Linux host works too because
#  the kernel is freestanding; see README "toolchain").
# =============================================================================

TARGET      := blackos
BUILD       := build
ISO_NAME    := $(TARGET).iso
ISO_ROOT    := iso
KERNEL_BIN  := $(BUILD)/kernel.bin
ELF         := $(BUILD)/kernel.elf

AS          := nasm
ASFLAGS     := -f elf64 -F dwarf -g

# --- locate a 64-bit C compiler and objcopy --------------------------------
XCC := $(shell for t in x86_64-elf-gcc x86_64-linux-gnu-gcc x86_64-pc-linux-gnu-gcc; do \
            command -v $$t >/dev/null 2>&1 && { echo $$t; exit 0; }; done)

ifeq ($(XCC),)
$(error "No 64-bit compiler found (need x86_64-elf-gcc or x86_64-linux-gnu-gcc). See README - toolchain.")
endif

XOBJCOPY := $(subst gcc,objcopy,$(XCC))
ifeq ($(shell command -v $(XOBJCOPY) 2>/dev/null),)
XOBJCOPY := objcopy
endif

CC     := $(XCC)
OBJCOPY := $(XOBJCOPY)

CFLAGS := -std=gnu11 -m64 \
          -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie \
          -fno-asynchronous-unwind-tables -fno-ident \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=small \
          -O2 -g \
          -Wall -Wextra -Wno-unused-parameter \
          -Isrc -Isrc/contrib

LDFLAGS := -T linker.ld -nostdlib -no-pie \
           -Wl,--build-id=none \
           -Wl,-z,max-page-size=0x1000

# CONFIG_TOOLCHAIN: prioritize PC-native toolchains. If unset, fall back to
# environment overrides in scripts/run.sh.
KSRC  := $(wildcard src/kernel/*.c)
KOBJ  := $(patsubst src/kernel/%.c,$(BUILD)/kernel/%.o,$(KSRC))

# Build-time toolchain guidance if a required binary is missing
CHECK_TOOLCHAIN := $(shell command -v nasm >/dev/null 2>&1 && command -v $(subst gcc,objcopy,$(CC)) >/dev/null 2>&1 && echo OK)
ifeq ($(CHECK_TOOLCHAIN),)
$(warning *** Toolchain incomplete -- no nasm/gcc/x86_64 toolchain found)
$(warning See README.md section "Toolchain setup")
endif
BOBJ  := $(BUILD)/boot/boot.o
KASSRC := $(wildcard src/kernel/*.s)
KASOBJ := $(patsubst src/kernel/%.s,$(BUILD)/kernel/%_asm.o,$(KASSRC))
KOBJ := $(KOBJ) $(KASOBJ)
CONTRIB := $(BUILD)/contrib/duktape.o
OBJS  := $(BOBJ) $(KOBJ) $(CONTRIB)

.PHONY: all iso run nographic verify stage clean mrproper

all: $(KERNEL_BIN)

# ---- compile --------------------------------------------------------------
$(BUILD)/boot/boot.o: src/boot/boot.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel/%_asm.o: src/kernel/%.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Engine TUs (float-heavy) need SSE2; kernel core stays on soft-float ABI.
$(BUILD)/kernel/libm.o:   CFLAGS += -msse2 -mfpmath=sse -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
$(BUILD)/kernel/kfmt.o:   CFLAGS += -msse2 -mfpmath=sse -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
$(BUILD)/kernel/ctx_js.o: CFLAGS += -msse2 -mfpmath=sse -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0

$(BUILD)/kernel/%.o: src/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CONTRIB): src/contrib/duktape.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -m64 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-ident -mno-red-zone -msse2 -mfpmath=sse -mcmodel=large -O2 -g -Wall -Wextra -Wno-unused-parameter -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Isrc -Isrc/contrib -c $< -o $@
# ---- link -----------------------------------------------------------------
$(ELF): $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(KERNEL_BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
	@echo ">>> $(KERNEL_BIN) ready (`stat -c %s $@ 2>/dev/null || printf 'built'` bytes)"

# ---- ISO staging + authoring ----------------------------------------------
stage: $(KERNEL_BIN)
	@mkdir -p $(ISO_ROOT)/boot/grub
	cp $(KERNEL_BIN) $(ISO_ROOT)/boot/$(TARGET).bin

$(ISO_NAME): $(KERNEL_BIN) $(ISO_ROOT)/boot/grub/grub.cfg stage
	grub-mkrescue -o $@ $(ISO_ROOT) --compress=no

iso: $(ISO_NAME)
	@echo ">>> $(ISO_NAME) ready (BIOS + UEFI hybrid)"

# ---- execution + verification ----------------------------------------------
run: iso
	./scripts/run.sh

nographic: iso
	./scripts/run.sh --nographic

verify: iso
	python3 scripts/verify.py

clean:
	rm -rf $(BUILD)

mrproper: clean
	rm -f $(ISO_NAME)
