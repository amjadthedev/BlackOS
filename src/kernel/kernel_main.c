/* =============================================================================
 *  Black OS - kernel entry point / Phase 6 bring-up + interactive console
 *
 *  Called from the long-mode stub with:
 *      RDI = multiboot2 information structure physical address
 *      RSI = 0x36D76289 (multiboot2 boot magic, re-verified here)
 * ========================================================================== */
#include "types.h"
#include "io.h"
#include "multiboot2.h"
#include "printk.h"
#include "vga.h"
#include "serial.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "auth.h"
#include "themes.h"
#include "app.h"
#include "wallpaper.h"
#include "audio.h"
#include "browser.h"

extern u8 __kernel_start[], __kernel_end[];

#include "heap.h"            /* heap_init() */
#include "io.h"              /* cpu_enable_sse() */
extern void ctx_js_init(void);
extern void ctx_js_destroy(void);

extern void comp_run(void);
extern void shell_launch_app(const struct app *a);
extern const struct app app_settings;
extern const struct app app_store;
extern const struct app app_youtube;
extern const struct app app_snake;
extern const struct app app_pong;
extern void auth_lock(void);
extern void themes_init(void);
extern int theme_count(void);
extern void apply_theme(int index);
extern int theme_active_index(void);
extern void wallpaper_set_solid(u32 color);
extern void wallpaper_set_gradient(u32 top, u32 bot);
extern void net_init(void);

#define KERNEL_VERSION   "Black OS 1.0.0"

/* ------------------------------------------------------------ telemetry ---- */
static void print_system_info(void)
{
    paging_report();
    pmm_stats();
    kprintf("[sys ] kernel image: %p - %p\n",
            __kernel_start, __kernel_end);
    kprintf("[sys ] uptime: %llu ms\n", uptime_ms);
}

/* ----------------------------------------------------------------- shell --- */
#define CMD_MAX 64

static char cmd_buf[CMD_MAX];
static usize cmd_len;

static void shell_prompt(void)
{
    vga_set_color(VGA_LRED, VGA_BLACK);
    kprintf("[black@blackos");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf(" / ]");
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("# ");
}

static void cmd_help(void)
{
    kprintf("commands:\n"
            "  help     this list\n"
            "  clear    clear the screen\n"
            "  info     kernel + MMU + memory telemetry\n"
            "  mem      detailed physical memory statistics\n"
            "  uptime   elapsed seconds since boot\n"
            "  banner   redraw the BLACK OS splash\n"
            "  reboot   pulse the 8042 reset line\n"
            "  halt     park the CPU (safe to power off)\n"
            "  pmmtest  run PMM selftest\n"
            "  maptest  test dynamic 2 MiB mapping\n"
            "  gui      launch graphical desktop shell\n"
            "  mountfat mount a FAT32 volume\n"
            "  netup    bring up network stack (e1000)\n"
            "  ping     ping a host\n"
            "  beep     generate a software beep\n"
            "  settings open Settings app\n"
            "  store    open App Store\n"
            "  youtube  open web browser\n"
            "  snake    play Snake\n"
            "  pong     play Pong\n"
            "  code     open code editor\n"
            "  wallpaper set gradient wallpaper\n"
            "  theme    cycle desktop theme\n"
            "  passwd   set account password\n"
            "  lock     show lock screen\n"
            "  userrun  run userspace demo task\n"
            "  webtest  run browser engine self-test (JS + CSS)\n"
            "  csstest  run CSS parser self-test\n");
}

static void shell_execute(void)
{
    if (cmd_len == 0)
        return;
    cmd_buf[cmd_len] = '\0';

    if (strcmp(cmd_buf, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd_buf, "clear") == 0) {
        vga_clear();
    } else if (strcmp(cmd_buf, "info") == 0) {
        print_system_info();
    } else if (strcmp(cmd_buf, "mem") == 0) {
        pmm_stats();
    } else if (strcmp(cmd_buf, "uptime") == 0) {
        kprintf("uptime: %llu.%03llu s  (%llu ms)\n",
                uptime_ms / 1000, uptime_ms % 1000, uptime_ms);
    } else if (strcmp(cmd_buf, "banner") == 0) {
        vga_banner();
    } else if (strcmp(cmd_buf, "reboot") == 0) {
        kprintf("[sys ] rebooting via 8042 pulse...\n");
        io_wait();
        outb(0x64, 0xFE);
        for (;;)
            hlt();
    } else if (strcmp(cmd_buf, "halt") == 0) {
        kprintf("[sys ] it is now safe to power off Black OS.\n");
        cli();
        for (;;)
            hlt();
    } else if (strcmp(cmd_buf, "pmmtest") == 0) {
        pmm_selftest();
    } else if (strcmp(cmd_buf, "maptest") == 0) {
        const u64 phys = 0x40000000ULL;
        int rc = paging_map_2mb(phys);
        if (rc < 0)
            kprintf("[mmu ] map FAILED for %P\n", (void *)(usize)phys);
        else
            kprintf("[mmu ] mapped (2 MiB) %P -> identical VA [%s]\n",
                    (void *)(usize)phys,
                    rc == 0 ? "new page-directory entry installed"
                            : "already mapped");
    } else if (strcmp(cmd_buf, "gui") == 0) {
        kprintf("[sys ] switching to graphical shell...\n");
        console_use_fb();
        comp_run();
    } else if (strcmp(cmd_buf, "mountfat") == 0) {
        kprintf("[sys ] mounting FAT32 volume...\n");
    } else if (strcmp(cmd_buf, "netup") == 0) {
        kprintf("[sys ] bringing up network stack...\n");
        net_init();
    } else if (strcmp(cmd_buf, "ping") == 0) {
        kprintf("[sys ] ping demo (press ENTER after host): \n");
    } else if (strcmp(cmd_buf, "beep") == 0) {
        kprintf("[sys ] beeping at 440 Hz...\n");
        audio_beep(440, 500);
    } else if (strcmp(cmd_buf, "settings") == 0) {
        console_use_fb();
        shell_launch_app(&app_settings);
    } else if (strcmp(cmd_buf, "store") == 0) {
        console_use_fb();
        shell_launch_app(&app_store);
    } else if (strcmp(cmd_buf, "youtube") == 0) {
        console_use_fb();
        shell_launch_app(&app_youtube);
    } else if (strcmp(cmd_buf, "snake") == 0) {
        console_use_fb();
        shell_launch_app(&app_snake);
    } else if (strcmp(cmd_buf, "pong") == 0) {
        console_use_fb();
        shell_launch_app(&app_pong);
    } else if (strcmp(cmd_buf, "code") == 0) {
        console_use_fb();
        shell_launch_app(&app_code);
    } else if (strcmp(cmd_buf, "wallpaper") == 0) {
        wallpaper_set_gradient(0x060A12, 0x010309);
        kprintf("[sys ] wallpaper set to gradient\n");
    } else if (strcmp(cmd_buf, "theme") == 0) {
        int next = (theme_active_index() + 1) % theme_count();
        apply_theme(next);
        kprintf("[sys ] theme: %s\n", theme_at(next)->name);
    } else if (strcmp(cmd_buf, "webtest") == 0) {
        kprintf("[webtest] launching browser engine self-test...\n");
        browser_load_demo();
    } else if (strcmp(cmd_buf, "csstest") == 0) {
        browser_css_test();
    } else if (strcmp(cmd_buf, "passwd") == 0) {
        kprintf("[sys ] enter new password: ");
    } else if (strcmp(cmd_buf, "lock") == 0) {
        auth_lock();
        kprintf("[sys ] locked\n");
    } else if (strcmp(cmd_buf, "userrun") == 0) {
        kprintf("[sys ] running user demo...\n");
    } else {
        kprintf("unknown command: %s\n", cmd_buf);
        kprintf("type 'help' for the command list\n");
    }
}

/* IRQ1 (keyboard) lands here through keyboard_set_hook(). */
static void shell_feed(char c)
{
    if (c == '\n') {
        kputc('\n');
        shell_execute();
        cmd_len = 0;
        shell_prompt();
        return;
    }

    if (c == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            kputc('\b');
            kputc(' ');
            kputc('\b');
        }
        return;
    }

    if ((u8)c < 0x20)
        return;                          /* drop control characters */

    if (cmd_len < CMD_MAX - 1) {
        cmd_buf[cmd_len++] = c;
        cmd_buf[cmd_len] = '\0';
        kputc(c);
    }
}

/* ------------------------------------------------------------------- boot --- */
void __attribute__((noreturn))
kmain(u64 mb_info_addr, u64 mb_magic)
{
    serial_init();                       /* logs must start before the display */

    vga_init();
    vga_banner();

    /* user-facing boot screen text */
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  BLACK OS boot system staged: press <Enter> to proceed\n");
    kprintf("  * Loading kernel...  * Loading VFS...  * CPU initialized\n");

    /* Who booted us?  GRUB's `multiboot` command speaks Multiboot v1
     * (EAX = 0x2BADB002, flat info at EBX); an MB2 loader reports
     * 0x36D76289 with a tag list.  boot.s passes EAX straight through. */
    if (mb_magic == MB1_BOOT_MAGIC)
        kprintf("[boot] loader magic OK (multiboot v1, EAX = %llx)\n", mb_magic);
    else if (mb_magic == MB2_BOOT_MAGIC)
        kprintf("[boot] loader magic OK (multiboot v2, EAX = %llx)\n", mb_magic);
    else
        panic("unexpected loader boot magic: %llx\n", mb_magic);

    kprintf("[boot] loader info structure at 0x%llx\n", mb_info_addr);

    /* Phase 4: exceptions + hardware interrupts. */
    keyboard_init();
    idt_init();
    pic_init();
    sti();                              /* PIC remapped: safe to enable now */
    pit_init(1000);                      /* 1 ms tick feeding uptime_ms */

    /* Phase 5: physical memory + paging. */
    pmm_init(mb_info_addr, mb_magic);
    pmm_stats();
    pmm_selftest();
    paging_report();

    /* JS engine backing + heap for Duktape: PMM is ready by now. */
    cpu_enable_sse();
    heap_init();
    css_init();
    ctx_js_init();

    /* Phase 13: auth + shell theming + wallpaper                           */
    auth_init();
    themes_init();
    wallpaper_init();

    kprintf("[boot] indexing kernel image: 0x%llx - 0x%llx\n",
            (u64)(usize)__kernel_start, (u64)(usize)__kernel_end);
    kprintf("[boot] %s ready.\n", KERNEL_VERSION);

    /* Success marker for scripts/verify.py (serial capture). */
    kputs("BLACKOS_BOOT_OK\n");

    /* Interactive console: keystrokes arrive from the PS/2 keyboard (IRQ1)
     * and from COM1 — scripts/verify.py drives the shell over serial.  The
     * PIT tick (1 kHz) wakes us out of hlt() every millisecond, so polling
     * the UART FIFO here is cheap and needs no UART IRQ. */
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    keyboard_set_hook(shell_feed);
    shell_prompt();

    for (;;) {
        char c;
        while (serial_try_getc(&c))
            shell_feed(c);
        hlt();
    }
}
