/* =============================================================================
 *  Black OS - audio.c : PC speaker + AC'97 PCM audio driver
 *
 *  PC speaker works on every x86 machine (and QEMU) via timer channel 2 +
 *  port 0x61.  AC'97 PCM playback uses DMA: samples are written to a PMM-
 *  allocated buffer, the AC'97 controller streams them to the codec via the
 *  AC-link, and the codec converts them to analog.
 * ========================================================================== */
#include "audio.h"
#include "io.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "printk.h"
#include "idt.h"

static int audio_dev = 0;          /* 0 = none, 1 = pc speaker, 2 = ac97   */
static u8  volume = 100;

/* AC'97 controller register offsets (Intel ICH native PCI mode)            */
#define AC97_GCNT      0x00   /* Global Control                          */
#define AC97_GSTS      0x04   /* Global Status                           */
#define AC97_POBA      0x10   /* PCM Out Buffer Address                  */
#define AC97_POLVI     0x18   /* PCM Out Last Valid Index                */
#define AC97_POBCL     0x1C   /* PCM Out Buffer Count/List               */
#define AC97_POCR      0x20   /* PCM Out Control                         */

/* AC'97 codec register indices (via AC-link)                               */
#define AC97_RESET       0x00
#define AC97_MASTER_VOL  0x04
#define AC97_PCM_DAC_VOL 0x08
#define AC97_PCM_DAC_RATE 0x0C

/* PCM buffer configuration                                                 */
#define PCM_FRAMES     512       /* stereo frames per buffer               */
#define PCM_BYTES      (PCM_FRAMES * 4)  /* 16-bit stereo = 4 bytes/frame   */

static u32 ac97_io_base = 0;       /* I/O port base of AC'97 controller    */
static u8  *pcm_dma_buf = NULL;    /* DMA buffer for PCM samples           */
static u64 pcm_dma_phys = 0;       /* physical address of DMA buffer       */
static u32 pcm_write_pos = 0;      /* next write position in DMA buffer    */

/* ── PC speaker (universal fallback) ──────────────────────────────────── */
static void pc_speaker_on(u32 frequency_hz)
{
    if (frequency_hz == 0)
        return;

    u32 div = 1193182 / frequency_hz;
    outb(0xB6, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));

    u8 tmp = inb(0x61);
    if ((tmp & 0x03) != 0x03)
        outb(0x61, tmp | 0x03);
}

static void pc_speaker_off(void)
{
    u8 tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

/* ── AC'97 controller I/O ────────────────────────────────────────────── */
static void ac97_outw(u16 reg, u16 val)
{
    outw(ac97_io_base + reg, val);
}

static u16 ac97_inw(u16 reg)
{
    return inw(ac97_io_base + reg);
}

/* Write to AC'97 codec register via AC-link                                */
static void ac97_codec_write(u8 reg, u16 val)
{
    /* Wait for codec write port to be ready                                */
    u32 spins = 0;
    while ((ac97_inw(AC97_GSTS) & 0x02) && spins < 100000)
        spins++;
    outw(ac97_io_base + AC97_CODEC_WRITE, (u16)((u16)val << 8) | reg);
}

/* Read from AC'97 codec register via AC-link                               */
static u16 ac97_codec_read(u8 reg)
{
    u32 spins = 0;
    while ((ac97_inw(AC97_GSTS) & 0x02) && spins < 100000)
        spins++;
    outw(ac97_io_base + AC97_CODEC_READ, reg);
    spins = 0;
    while (!(ac97_inw(AC97_GSTS) & 0x01) && spins < 100000)
        spins++;
    return inw(ac97_io_base + AC97_CODEC_READ) & 0xFFFF;
}

/* ── AC'97 codec init ─────────────────────────────────────────────────── */
static void ac97_init_codec(void)
{
    /* Reset the codec                                                      */
    ac97_codec_write(AC97_RESET, 0x0000);
    for (volatile int k = 0; k < 10000; k++)
        ;

    /* Wait for codec to be ready                                           */
    u16 status = ac97_codec_read(AC97_RESET);
    if ((status & 0x7F00) == 0x7F00) {
        printk("[audio] AC'97 codec not responding\n");
        return;
    }

    /* Set master volume (0x04) - 0x8000 = mute, lower = louder             */
    u16 master_vol = (u16)((100 - volume) * 255 / 100);
    master_vol = (master_vol << 8) | master_vol;
    ac97_codec_write(AC97_MASTER_VOL, master_vol);

    /* Set PCM DAC volume (0x08)                                            */
    u16 pcm_vol = (u16)((100 - volume) * 255 / 100);
    pcm_vol = (pcm_vol << 8) | pcm_vol;
    ac97_codec_write(AC97_PCM_DAC_VOL, pcm_vol);

    /* Set PCM DAC sample rate to 48000 Hz (0x0C)                           */
    ac97_codec_write(AC97_PCM_DAC_RATE, 0xBB80);  /* 48000 */

    printk("[audio] AC'97 codec initialized (volume=%u%%)", volume);
}

/* ── AC'97 controller init ────────────────────────────────────────────── */
static void ac97_init_controller(void)
{
    /* Soft reset the controller                                            */
    ac97_outw(AC97_GCNT, 0x0001);
    for (volatile int k = 0; k < 10000; k++)
        ;
    ac97_outw(AC97_GCNT, 0x0000);

    /* Wait for controller ready                                            */
    u32 spins = 0;
    while (!(ac97_inw(AC97_GSTS) & 0x04) && spins < 100000)
        spins++;

    /* Allocate DMA buffer for PCM                                          */
    pcm_dma_buf = (u8 *)(usize)pmm_alloc_run((PCM_BYTES + 4095) / 4096);
    if (pcm_dma_buf == NULL) {
        printk("[audio] out of DMA memory\n");
        return;
    }
    pcm_dma_phys = (u64)(usize)pcm_dma_buf;
    memset(pcm_dma_buf, 0, PCM_BYTES);

    /* Program PCM out buffer address                                       */
    ac97_outw(AC97_POBA, (u16)(pcm_dma_phys & 0xFFFFFFFFu));
    /* For 32-bit systems, upper bits not needed                            */

    /* Program buffer size (in bytes - 1)                                   */
    ac97_outw(AC97_POBCL, (u16)(PCM_BYTES - 1));

    /* Program control: enable interrupts, DMA, etc.                        */
    ac97_outw(AC97_POCR, 0x0001);  /* start DMA playback                     */

    ac97_init_codec();
}

/* ── PCM sample write ─────────────────────────────────────────────────── */
/* Write 16-bit stereo samples to the DMA buffer.  The AC'97 controller
 * streams them to the codec automatically.                                 */
static void pcm_write_samples(const void *samples, u32 num_frames)
{
    if (pcm_dma_buf == NULL)
        return;

    u32 bytes = num_frames * 4;
    if (pcm_write_pos + bytes > PCM_BYTES)
        bytes = PCM_BYTES - pcm_write_pos;
    if (bytes == 0)
        return;

    memcpy(pcm_dma_buf + pcm_write_pos, samples, bytes);
    pcm_write_pos += bytes;
    if (pcm_write_pos >= PCM_BYTES)
        pcm_write_pos = 0;
}

/* ── public API ───────────────────────────────────────────────────────── */
void audio_init(void)
{
    volume = 100;

    /* Scan PCI for multimedia audio device (class 0x04, subclass 0x01)    */
    struct pci_dev dev;
    if (pci_scan_class(0x04, &dev) == 0) {
        /* AC'97 controllers use I/O port BAR0                              */
        u32 bar0 = dev.bar0_info;
        if ((bar0 & 0x1) != 0) {           /* I/O port BAR                */
            ac97_io_base = bar0 & 0xFFFCu;
            ac97_init_controller();
            audio_dev = 2;
            printk("[audio] AC'97 controller at I/O 0x%x\n", ac97_io_base);
            return;
        }
    }

    /* Fall back to PC speaker                                              */
    audio_dev = 1;
    printk("[audio] PC speaker audio enabled\n");
}

void audio_beep(u32 frequency_hz, u32 duration_ms)
{
    if (audio_dev == 0)
        return;
    if (volume == 0)
        return;

    if (audio_dev == 1)
        pc_speaker_on(frequency_hz);
    else if (audio_dev == 2) {
        /* Generate a simple square wave in PCM for AC'97                   */
        u32 sample_rate = 48000;
        u32 total_samples = sample_rate * duration_ms / 1000;
        u32 half_period = sample_rate / (2 * frequency_hz);
        u16 samples[256];
        u32 written = 0;

        while (written < total_samples) {
            u32 chunk = total_samples - written;
            if (chunk > 256)
                chunk = 256;
            u16 amp = (u16)(volume * 3000 / 100);
            for (u32 k = 0; k < chunk; k++) {
                u16 val = ((written + k) / half_period) & 1 ? amp : 0;
                samples[k * 2] = val;     /* left  */
                samples[k * 2 + 1] = val; /* right */
            }
            pcm_write_samples(samples, chunk);
            written += chunk;
        }
    }

    u32 end = uptime_ms + duration_ms;
    while (uptime_ms < end)
        hlt();

    audio_stop();
}

void audio_stop(void)
{
    if (audio_dev == 1)
        pc_speaker_off();
    else if (audio_dev == 2) {
        pcm_write_pos = 0;
        if (pcm_dma_buf)
            memset(pcm_dma_buf, 0, PCM_BYTES);
    }
}

int audio_present(void)   { return audio_dev != 0; }

void audio_set_volume(u8 p)
{
    volume = p;
    if (audio_dev == 2) {
        u16 master_vol = (u16)((100 - p) * 255 / 100);
        master_vol = (master_vol << 8) | master_vol;
        ac97_codec_write(AC97_MASTER_VOL, master_vol);
        u16 pcm_vol = (u16)((100 - p) * 255 / 100);
        pcm_vol = (pcm_vol << 8) | pcm_vol;
        ac97_codec_write(AC97_PCM_DAC_VOL, pcm_vol);
    }
}
