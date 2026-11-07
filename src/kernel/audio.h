/* =============================================================================
 *  Black OS - audio.c / audio.h : PC speaker + AC'97 audio stub
 *
 *  PC speaker works on every x86 machine (and QEMU) via timer channel 2 +
 *  port 0x61.  AC'97 detection is included as a stub so you can wire a real
 *  codec driver later without changing the app API.
 * ========================================================================== */
#ifndef BLACKOS_AUDIO_H
#define BLACKOS_AUDIO_H

#include "types.h"

void audio_init(void);                         /* detect AC'97, setup PC spk */
void audio_beep(u32 frequency_hz, u32 duration_ms);
void audio_stop(void);
int  audio_present(void);                      /* 1 if any audio device found  */
void audio_set_volume(u8 percent);             /* 0..100                       */

/* AC'97 codec registers (discovered via PCI, not yet implemented)          */
#define AC97_RESET        0x00
#define AC97_PCM_VOLUME   0x18
#define AC97_CODEC_READ   0x10   /* codec read port offset from I/O base   */
#define AC97_CODEC_WRITE  0x10   /* codec write port offset from I/O base  */

#endif /* BLACKOS_AUDIO_H */