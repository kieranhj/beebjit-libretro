#ifndef BEEBJIT_OS_SOUND_LIBRETRO_H
#define BEEBJIT_OS_SOUND_LIBRETRO_H

#include <stdint.h>

/* Drain up to max_frames mono frames produced by the BBC thread.
 * Returns the number of frames copied. Safe to call while the BBC thread is
 * parked at a vsync (the only time the core calls it).
 */
uint32_t os_sound_libretro_read(int16_t* p_out, uint32_t max_frames);

#endif /* BEEBJIT_OS_SOUND_LIBRETRO_H */
