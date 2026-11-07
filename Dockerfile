FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-13-x86-64-linux-gnu \
        binutils-x86-64-linux-gnu \
        nasm \
        grub-pc-bin \
        grub-efi-amd64-bin \
        grub-common \
        xorriso \
        mtools \
        qemu-system-x86 \
        python3 \
        dosfstools \
    && rm -rf /var/lib/apt/lists/*

# Expose the cross toolchain under the names the Makefile probes for:
#   x86_64-elf-gcc  and  x86_64-linux-gnu-gcc   -> C compilation
#   x86_64-elf-objcopy and x86_64-linux-gnu-objcopy -> ELF -> flat binary
RUN ln -sf /usr/bin/x86_64-linux-gnu-gcc-13 /usr/local/bin/x86_64-elf-gcc && \
    ln -sf /usr/bin/x86_64-linux-gnu-gcc-13 /usr/bin/x86_64-linux-gnu-gcc && \
    ln -sf /usr/bin/x86_64-linux-gnu-objcopy /usr/local/bin/x86_64-elf-objcopy

WORKDIR /black
CMD ["/bin/bash"]
